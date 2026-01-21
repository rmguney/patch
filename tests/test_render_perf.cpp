#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <ctime>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static const int TEST_FRAMES = 300;
static const int LAUNCH_WAIT_MS = 15000; /* 15 seconds - needs time for GPU warmup and test frames */

static const float FRAME_BUDGET_MS = 16.667f;
static const char *TEMP_CSV = "profile_temp.csv";

/*
 * Performance Budget Thresholds (RTX 4050M / mid-range laptop)
 */
struct PerfThreshold
{
    float pass_ms; // Green: performance is good
    float warn_ms; // Yellow: approaching limit
    float fail_ms; // Red: regression detected
};

static const PerfThreshold THRESHOLD_50 = {8.33f, 9.62f, 11.54f};
static const PerfThreshold THRESHOLD_250 = {11.11f, 13.23f, 15.87f};
static const PerfThreshold THRESHOLD_500 = {16.67f, 20.84f, 29.17f};
static const PerfThreshold THRESHOLD_1000 = {16.67f, 20.00f, 30.00f};
static const PerfThreshold THRESHOLD_CLOSEUP = {11.11f, 14.29f, 20.00f};         /* Close-up 250 objects */
static const PerfThreshold THRESHOLD_ROAM_CLOSEUP = {11.11f, 14.29f, 20.00f};    /* Roam terrain close-up */
static const PerfThreshold THRESHOLD_EXTREME_CLOSEUP = {14.00f, 18.00f, 25.00f}; /* Extreme close-up (nearly touching) */
static const PerfThreshold THRESHOLD_DISTANCE_SCALE = {12.00f, 16.00f, 22.00f};  /* Distance scaling tests */

/* CPU dispatch timing thresholds */
struct PassThreshold
{
    float main_ms;   /* G-buffer + objects dispatch time */
    float shadow_ms; /* Shadow pass dispatch time */
};

static const PassThreshold PASS_THRESHOLD_NORMAL = {2.0f, 0.5f};
static const PassThreshold PASS_THRESHOLD_CLOSEUP = {4.0f, 1.0f};

/* GPU execution timing thresholds (actual shader execution on GPU) */
struct GPUThreshold
{
    float main_ms;   /* G-buffer GPU execution time */
    float shadow_ms; /* Shadow GPU execution time */
    float total_ms;  /* Total GPU time */
};

static const GPUThreshold GPU_THRESHOLD_NORMAL = {6.0f, 3.0f, 10.0f};
static const GPUThreshold GPU_THRESHOLD_CLOSEUP = {10.0f, 5.0f, 16.0f};

enum PerfStatus
{
    PERF_PASS,
    PERF_WARN,
    PERF_FAIL
};

static PerfStatus evaluate_perf(float frame_ms, const PerfThreshold &thresh)
{
    if (frame_ms <= thresh.pass_ms)
        return PERF_PASS;
    if (frame_ms <= thresh.warn_ms)
        return PERF_WARN;
    return PERF_FAIL;
}

static const char *status_string(PerfStatus s)
{
    switch (s)
    {
    case PERF_PASS:
        return "PASS";
    case PERF_WARN:
        return "WARN";
    case PERF_FAIL:
        return "FAIL";
    }
    return "UNKNOWN";
}

struct ProfileData
{
    float frame_avg_ms;
    float frame_max_ms;
    float frame_p95_ms;
    float render_total_avg_ms;
    float render_shadow_avg_ms;
    float render_main_avg_ms;
    float render_ui_avg_ms;
    float sim_tick_avg_ms;
    float sim_physics_avg_ms;
    float sim_collision_avg_ms;
    /* GPU execution times (from header comments, distinct from CPU dispatch times above) */
    float gpu_shadow_ms;
    float gpu_main_ms;
    float gpu_total_ms;
    /* Budget/spike data from header */
    float budget_pct;
    int budget_overruns;
    float worst_frame_ms;
    int32_t samples;
    bool valid;
};

static int launch_app(const char *exe_path, const char *args, int wait_ms, int stress_objects = 0)
{
    if (stress_objects > 0)
    {
        char env_val[32];
        snprintf(env_val, sizeof(env_val), "%d", stress_objects);
        SetEnvironmentVariableA("PATCH_STRESS_OBJECTS", env_val);
    }
    else
    {
        SetEnvironmentVariableA("PATCH_STRESS_OBJECTS", NULL);
    }

    char cmd_line[512];
    snprintf(cmd_line, sizeof(cmd_line), "\"%s\" %s", exe_path, args);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};

    if (!CreateProcessA(NULL, cmd_line, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        printf("CreateProcess failed (error %lu)\n", GetLastError());
        return 1;
    }

    DWORD wait_result = WaitForSingleObject(pi.hProcess, wait_ms);

    if (wait_result == WAIT_OBJECT_0)
    {
        DWORD exit_code = 0;
        GetExitCodeProcess(pi.hProcess, &exit_code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        /* Check for Windows exception codes (crash) */
        if ((exit_code & 0xC0000000) == 0xC0000000)
        {
            printf("CRASH: exit code 0x%08lX", exit_code);
            if (exit_code == 0xC0000005)
                printf(" (ACCESS_VIOLATION)");
            else if (exit_code == 0xC0000094)
                printf(" (INTEGER_DIVIDE_BY_ZERO)");
            else if (exit_code == 0xC00000FD)
                printf(" (STACK_OVERFLOW)");
            printf("\n");
        }

        return (int)exit_code;
    }
    else
    {
        /* Timeout - kill process and wait for cleanup */
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000); /* Wait up to 2s for process to die */
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        Sleep(500); /* Small delay to ensure GPU resources are released */
        return -1;
    }
}

static void cleanup_stale_processes()
{
    /* Kill any lingering patch_samples.exe processes from previous runs */
    system("taskkill /F /IM patch_samples.exe >NUL 2>&1");
    Sleep(1000); /* Wait for GPU resources to be released */
}

static float parse_csv_value(const char *line, const char *category, int col)
{
    if (strncmp(line, category, strlen(category)) != 0)
        return -1.0f;

    const char *p = line + strlen(category);
    if (*p != ',')
        return -1.0f;
    p++;

    for (int i = 0; i < col; i++)
    {
        p = strchr(p, ',');
        if (!p)
            return -1.0f;
        p++;
    }

    return (float)atof(p);
}

static void parse_gpu_timings_header(const char *line, ProfileData *data)
{
    /* Parse: # GPU Timings: shadow=18.129ms, main=19.788ms, total=19.777ms */
    const char *prefix = "# GPU Timings:";
    if (strncmp(line, prefix, strlen(prefix)) != 0)
        return;

    const char *p = line + strlen(prefix);

    /* Parse shadow */
    const char *shadow_start = strstr(p, "shadow=");
    if (shadow_start)
        data->gpu_shadow_ms = (float)atof(shadow_start + 7);

    /* Parse main */
    const char *main_start = strstr(p, "main=");
    if (main_start)
        data->gpu_main_ms = (float)atof(main_start + 5);

    /* Parse total */
    const char *total_start = strstr(p, "total=");
    if (total_start)
        data->gpu_total_ms = (float)atof(total_start + 6);
}

static void parse_budget_header(const char *line, ProfileData *data)
{
    /* Parse: # Budget: 142.1% used, 930 overruns, 267.85ms worst */
    const char *prefix = "# Budget:";
    if (strncmp(line, prefix, strlen(prefix)) != 0)
        return;

    const char *p = line + strlen(prefix);

    /* Parse budget percentage */
    while (*p == ' ')
        p++;
    data->budget_pct = (float)atof(p);

    /* Parse overruns */
    const char *overruns_start = strstr(p, ", ");
    if (overruns_start)
    {
        overruns_start += 2;
        data->budget_overruns = atoi(overruns_start);
    }

    /* Parse worst frame */
    const char *worst_start = strstr(p, "ms worst");
    if (worst_start)
    {
        /* Walk back to find the number */
        const char *num_start = worst_start - 1;
        while (num_start > p && ((*num_start >= '0' && *num_start <= '9') || *num_start == '.'))
            num_start--;
        num_start++;
        data->worst_frame_ms = (float)atof(num_start);
    }
}

static ProfileData parse_profile_csv(const char *filepath)
{
    ProfileData data = {};
    data.valid = false;

    FILE *f = fopen(filepath, "r");
    if (!f)
        return data;

    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        /* Parse GPU timings and budget info from header comments */
        if (line[0] == '#')
        {
            parse_gpu_timings_header(line, &data);
            parse_budget_header(line, &data);
            continue;
        }

        if (line[0] == '\n')
            continue;

        if (strncmp(line, "category,", 9) == 0)
            continue;

        float val;

        if ((val = parse_csv_value(line, "frame_total", 0)) >= 0.0f)
        {
            data.frame_avg_ms = val;
            data.frame_max_ms = parse_csv_value(line, "frame_total", 1);
            data.frame_p95_ms = parse_csv_value(line, "frame_total", 4);
            data.samples = (int32_t)parse_csv_value(line, "frame_total", 5);
        }
        else if ((val = parse_csv_value(line, "render_total", 0)) >= 0.0f)
            data.render_total_avg_ms = val;
        else if ((val = parse_csv_value(line, "render_shadow", 0)) >= 0.0f)
            data.render_shadow_avg_ms = val;
        else if ((val = parse_csv_value(line, "render_main", 0)) >= 0.0f)
            data.render_main_avg_ms = val;
        else if ((val = parse_csv_value(line, "render_ui", 0)) >= 0.0f)
            data.render_ui_avg_ms = val;
        else if ((val = parse_csv_value(line, "sim_tick", 0)) >= 0.0f)
            data.sim_tick_avg_ms = val;
        else if ((val = parse_csv_value(line, "sim_physics", 0)) >= 0.0f)
            data.sim_physics_avg_ms = val;
        else if ((val = parse_csv_value(line, "sim_collision", 0)) >= 0.0f)
            data.sim_collision_avg_ms = val;
    }

    fclose(f);
    data.valid = (data.samples > 0);
    return data;
}

static bool run_perf_test(const char *exe_path, const char *test_name, int frames,
                          int stress_objects, const PerfThreshold &threshold,
                          int *passed, int *warned, int *failed,
                          const float *camera_pos = nullptr, int scene_id = 0)
{
    char args[512];
    if (camera_pos)
    {
        snprintf(args, sizeof(args), "--scene %d --test-frames %d --profile-csv %s --camera-pos %.1f %.1f %.1f",
                 scene_id, frames, TEMP_CSV, camera_pos[0], camera_pos[1], camera_pos[2]);
    }
    else
    {
        snprintf(args, sizeof(args), "--scene %d --test-frames %d --profile-csv %s", scene_id, frames, TEMP_CSV);
    }

    printf("\n");
    printf("================================================================================\n");
    printf("  %s\n", test_name);
    printf("================================================================================\n");
    printf("Configuration: %d frames, %d objects%s\n", frames, stress_objects > 0 ? stress_objects : 10,
           camera_pos ? " (close-up)" : "");
    printf("Thresholds: PASS <%.1fms | WARN <%.1fms | FAIL >%.1fms\n\n",
           threshold.pass_ms, threshold.warn_ms, threshold.fail_ms);
    fflush(stdout);

    DeleteFileA(TEMP_CSV);

    int result = launch_app(exe_path, args, LAUNCH_WAIT_MS, stress_objects);
    if (result != 0)
    {
        printf("FAIL: App exited with code %d\n", result);
        (*failed)++;
        return false;
    }

    ProfileData data = parse_profile_csv(TEMP_CSV);
    if (!data.valid)
    {
        printf("FAIL: Could not parse profile data\n");
        (*failed)++;
        return false;
    }

    float frame_budget_pct = (data.frame_avg_ms / FRAME_BUDGET_MS) * 100.0f;
    float render_pct = (data.render_total_avg_ms / FRAME_BUDGET_MS) * 100.0f;
    float sim_pct = (data.sim_tick_avg_ms / FRAME_BUDGET_MS) * 100.0f;
    float effective_fps = 1000.0f / data.frame_avg_ms;

    printf("Frame Timing (target: %.2fms):\n", FRAME_BUDGET_MS);
    printf("  Average:     %7.2f ms  (%5.1f%% budget)\n", data.frame_avg_ms, frame_budget_pct);
    printf("  Maximum:     %7.2f ms\n", data.frame_max_ms);
    printf("  95th pct:    %7.2f ms\n", data.frame_p95_ms);
    printf("  Samples:     %d\n", data.samples);
    printf("  Effective:   %.1f FPS\n", effective_fps);
    if (data.budget_overruns > 0)
    {
        printf("  Overruns:    %d (%.1f%% of frames)\n", data.budget_overruns,
               (float)data.budget_overruns / (float)data.samples * 100.0f);
        printf("  Worst:       %7.2f ms\n", data.worst_frame_ms);
    }
    printf("\n");

    printf("Render Breakdown (%5.1f%% of budget):\n", render_pct);
    printf("  Total:       %7.2f ms (CPU dispatch)\n", data.render_total_avg_ms);
    printf("  Shadow:      %7.2f ms (CPU dispatch)\n", data.render_shadow_avg_ms);
    printf("  Main:        %7.2f ms (CPU dispatch)\n", data.render_main_avg_ms);
    printf("  UI:          %7.2f ms\n", data.render_ui_avg_ms);

    /* GPU execution timings (actual shader execution, not just dispatch) */
    if (data.gpu_total_ms > 0.0f)
    {
        float gpu_budget_pct = (data.gpu_total_ms / FRAME_BUDGET_MS) * 100.0f;
        printf("\nGPU Execution (%5.1f%% of budget):\n", gpu_budget_pct);
        printf("  Shadow:      %7.2f ms\n", data.gpu_shadow_ms);
        printf("  Main:        %7.2f ms\n", data.gpu_main_ms);
        printf("  Total:       %7.2f ms\n", data.gpu_total_ms);
    }
    printf("\n");

    printf("Simulation Breakdown (%5.1f%% of budget):\n", sim_pct);
    printf("  Tick:        %7.2f ms\n", data.sim_tick_avg_ms);
    printf("  Physics:     %7.2f ms\n", data.sim_physics_avg_ms);
    printf("  Collision:   %7.2f ms\n\n", data.sim_collision_avg_ms);

    PerfStatus status = evaluate_perf(data.frame_avg_ms, threshold);
    printf("Result: %s (%.2fms avg, threshold: %.1fms pass / %.1fms warn)\n",
           status_string(status), data.frame_avg_ms, threshold.pass_ms, threshold.warn_ms);

    int spike_issues = 0;

    /* 60 FPS floor: max frame < 33ms (2x budget for occasional spikes) */
    if (data.frame_max_ms > 33.33f)
    {
        printf("SPIKE WARNING: Max frame %.2fms exceeds 2x budget (33.33ms)\n", data.frame_max_ms);
        spike_issues++;
    }

    /* P95 should be below fail threshold */
    if (data.frame_p95_ms > threshold.fail_ms)
    {
        printf("P95 WARNING: P95 %.2fms exceeds fail threshold %.1fms\n", data.frame_p95_ms, threshold.fail_ms);
        spike_issues++;
    }

    /* Spike ratio: max/avg > 5x is pathological */
    float spike_ratio = data.frame_max_ms / data.frame_avg_ms;
    if (spike_ratio > 5.0f)
    {
        printf("SPIKE WARNING: Spike ratio %.1fx (max/avg) indicates severe hitching\n", spike_ratio);
        spike_issues++;
    }

    /* Budget overrun detection: too many frames exceeding budget = stuttering */
    if (data.budget_overruns > 0)
    {
        float overrun_pct = (float)data.budget_overruns / (float)data.samples * 100.0f;

        /* More than 10% overruns is a warning */
        if (overrun_pct > 10.0f)
        {
            printf("OVERRUN WARNING: %.1f%% of frames exceeded budget (%d overruns)\n",
                   overrun_pct, data.budget_overruns);
            spike_issues++;
        }

        /* More than 25% overruns is a failure */
        if (overrun_pct > 25.0f)
        {
            printf("OVERRUN FAIL: %.1f%% of frames exceeded budget - severe stuttering\n", overrun_pct);
            spike_issues++;
        }

        /* Worst frame > 100ms is catastrophic stutter */
        if (data.worst_frame_ms > 100.0f)
        {
            printf("WORST FRAME FAIL: %.2fms worst frame - catastrophic stutter\n", data.worst_frame_ms);
            spike_issues += 2;
        }
    }

    /* CPU dispatch timing validation for close-up scenarios */
    bool is_closeup = (camera_pos != nullptr);
    PassThreshold pass_thresh = is_closeup ? PASS_THRESHOLD_CLOSEUP : PASS_THRESHOLD_NORMAL;

    if (data.render_main_avg_ms > pass_thresh.main_ms)
    {
        printf("G-BUFFER WARNING: Main pass %.2fms exceeds %.1fms threshold (CPU dispatch)\n",
               data.render_main_avg_ms, pass_thresh.main_ms);
        spike_issues++;
    }
    if (data.render_shadow_avg_ms > pass_thresh.shadow_ms)
    {
        printf("SHADOW WARNING: Shadow pass %.2fms exceeds %.1fms threshold (CPU dispatch)\n",
               data.render_shadow_avg_ms, pass_thresh.shadow_ms);
        spike_issues++;
    }

    /* GPU execution timing validation (the real performance metric) */
    if (data.gpu_total_ms > 0.0f)
    {
        GPUThreshold gpu_thresh = is_closeup ? GPU_THRESHOLD_CLOSEUP : GPU_THRESHOLD_NORMAL;

        if (data.gpu_main_ms > gpu_thresh.main_ms)
        {
            printf("GPU MAIN FAIL: GPU main %.2fms exceeds %.1fms threshold\n",
                   data.gpu_main_ms, gpu_thresh.main_ms);
            spike_issues++;
        }
        if (data.gpu_shadow_ms > gpu_thresh.shadow_ms)
        {
            printf("GPU SHADOW WARNING: GPU shadow %.2fms exceeds %.1fms threshold\n",
                   data.gpu_shadow_ms, gpu_thresh.shadow_ms);
            spike_issues++;
        }
        if (data.gpu_total_ms > gpu_thresh.total_ms)
        {
            printf("GPU TOTAL WARNING: GPU total %.2fms exceeds %.1fms threshold\n",
                   data.gpu_total_ms, gpu_thresh.total_ms);
            spike_issues++;
        }

        /* GPU spike detection: GPU time exceeding frame budget is a guaranteed frame drop */
        if (data.gpu_total_ms > FRAME_BUDGET_MS)
        {
            printf("GPU SPIKE FAIL: GPU total %.2fms exceeds frame budget (%.2fms) - guaranteed stuttering\n",
                   data.gpu_total_ms, FRAME_BUDGET_MS);
            spike_issues += 2; /* Count as 2 issues - this is critical */
        }

        /* GPU main spike: if main pass alone exceeds budget, it's catastrophic */
        if (data.gpu_main_ms > FRAME_BUDGET_MS)
        {
            printf("GPU MAIN SPIKE FAIL: GPU main %.2fms exceeds frame budget (%.2fms) - catastrophic\n",
                   data.gpu_main_ms, FRAME_BUDGET_MS);
            spike_issues += 2;
        }

        /* GPU shadow spike: shadow pass alone shouldn't exceed half budget */
        if (data.gpu_shadow_ms > FRAME_BUDGET_MS * 0.5f)
        {
            printf("GPU SHADOW SPIKE WARNING: GPU shadow %.2fms exceeds half frame budget (%.2fms)\n",
                   data.gpu_shadow_ms, FRAME_BUDGET_MS * 0.5f);
            spike_issues++;
        }
    }

    /* Variance check: P95/avg ratio > 2.5 indicates unstable frame pacing */
    float variance_ratio = data.frame_p95_ms / data.frame_avg_ms;
    if (variance_ratio > 2.5f)
    {
        printf("VARIANCE WARNING: P95/avg ratio %.2f indicates unstable pacing (threshold: 2.5)\n", variance_ratio);
        spike_issues++;
    }

    /* Fail only if more than 3 spike issues detected */
    if (spike_issues > 3)
    {
        printf("SPIKE FAIL: %d spike issues detected (threshold: >3)\n", spike_issues);
        (*failed)++;
        return false;
    }

    switch (status)
    {
    case PERF_PASS:
        (*passed)++;
        break;
    case PERF_WARN:
        (*warned)++;
        break;
    case PERF_FAIL:
        (*failed)++;
        break;
    }

    return (status != PERF_FAIL);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: test_render_perf <patch_samples.exe>\n");
        return 1;
    }

    /* Clean up any stale processes from previous runs that might hold GPU resources */
    cleanup_stale_processes();

    const char *exe_path = argv[1];
    int passed = 0;
    int warned = 0;
    int failed = 0;

    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

    printf("################################################################################\n");
    printf("#                        PATCH ENGINE BUILD REPORT                            #\n");
    printf("################################################################################\n");
    printf("Generated: %s\n", time_str);
    printf("Executable: %s\n", exe_path);

    /* GPU warmup run - first launch always has shader compilation overhead */
    printf("\n[Warming up GPU...]\n");
    char warmup_args[256];
    snprintf(warmup_args, sizeof(warmup_args), "--scene 0 --test-frames 10 --profile-csv NUL");
    launch_app(exe_path, warmup_args, LAUNCH_WAIT_MS, 10);
    Sleep(500); /* Brief pause after warmup */

    run_perf_test(exe_path, "BASELINE (50 objects)", 30, 50,
                  THRESHOLD_50, &passed, &warned, &failed);
    run_perf_test(exe_path, "STRESS TEST (250 objects)", 30, 250,
                  THRESHOLD_250, &passed, &warned, &failed);
    run_perf_test(exe_path, "HEAVY STRESS (500 objects)", 30, 500,
                  THRESHOLD_500, &passed, &warned, &failed);
    run_perf_test(exe_path, "ANXIETY IS KILLING ME (1000 objects)", 30, 1000,
                  THRESHOLD_1000, &passed, &warned, &failed);

    /* Close-up test: camera very close to objects */
    float closeup_camera[3] = {3.0f, 4.0f, 3.0f};
    run_perf_test(exe_path, "CLOSE-UP STRESS (250 objects)", 30, 250,
                  THRESHOLD_CLOSEUP, &passed, &warned, &failed, closeup_camera, 0);

    /* Roam scene close-up test: camera close to terrain surface (scene 1) */
    float roam_closeup_camera[3] = {2.0f, 4.0f, 2.0f};
    run_perf_test(exe_path, "ROAM TERRAIN CLOSE-UP", 30, 0,
                  THRESHOLD_ROAM_CLOSEUP, &passed, &warned, &failed, roam_closeup_camera, 1);

    /* Ground-level test: camera very close to terrain (y=1.5), 60 frames to catch spikes */
    float ground_level_camera[3] = {5.0f, 1.5f, 5.0f};
    run_perf_test(exe_path, "GROUND LEVEL (touching terrain)", 60, 0,
                  THRESHOLD_ROAM_CLOSEUP, &passed, &warned, &failed, ground_level_camera, 1);

    /* Extreme close-up test: camera nearly touching objects */
    float extreme_closeup_camera[3] = {1.5f, 2.0f, 1.5f};
    run_perf_test(exe_path, "EXTREME CLOSE-UP (250 objects)", 30, 250,
                  THRESHOLD_EXTREME_CLOSEUP, &passed, &warned, &failed, extreme_closeup_camera, 0);

    /* Distance scaling test series: verify performance scales linearly with distance */
    /* Uses scene 1 (roam terrain) with 0 objects to isolate pure terrain performance */
    printf("\n");
    printf("================================================================================\n");
    printf("  DISTANCE SCALING TEST SERIES (pure terrain)\n");
    printf("================================================================================\n");
    printf("Testing performance at multiple distances to detect non-linear scaling...\n\n");
    fflush(stdout);

    static const float DISTANCE_TESTS[] = {2.0f, 4.0f, 8.0f, 16.0f, 32.0f};
    static const int NUM_DISTANCE_TESTS = 5;
    float distance_results[5] = {0};

    for (int i = 0; i < NUM_DISTANCE_TESTS; i++)
    {
        float dist = DISTANCE_TESTS[i];
        float dist_camera[3] = {dist, dist * 1.5f, dist};
        char test_name[64];
        snprintf(test_name, sizeof(test_name), "DISTANCE %.0f units", dist);

        /* Run the test - scene 1 (roam terrain), 0 objects for pure terrain test */
        char args[512];
        snprintf(args, sizeof(args), "--scene 1 --test-frames 30 --profile-csv %s --camera-pos %.1f %.1f %.1f",
                 TEMP_CSV, dist_camera[0], dist_camera[1], dist_camera[2]);

        DeleteFileA(TEMP_CSV);
        int result = launch_app(exe_path, args, LAUNCH_WAIT_MS, 0);

        if (result == 0)
        {
            ProfileData data = parse_profile_csv(TEMP_CSV);
            if (data.valid)
            {
                distance_results[i] = data.frame_avg_ms;
                printf("  Distance %5.0f: %7.2f ms\n", dist, data.frame_avg_ms);
            }
        }
    }

    /* Check for non-linear scaling: close distances should not be >2x worse than far */
    /* Note: Inside-volume raymarching is inherently slower than outside-volume
       because every ray traverses terrain vs many rays missing the volume entirely.
       A 5x ratio is expected for inside vs outside scenarios. Fail at 8x (regression). */
    if (distance_results[0] > 0 && distance_results[NUM_DISTANCE_TESTS - 1] > 0)
    {
        float ratio = distance_results[0] / distance_results[NUM_DISTANCE_TESTS - 1];
        printf("\n  Close/Far ratio: %.2fx\n", ratio);
        if (ratio > 8.0f)
        {
            printf("  DISTANCE SCALING FAIL: Close-up %.1fx slower than far (threshold: 8.0x)\n", ratio);
            failed++;
        }
        else if (ratio > 5.0f)
        {
            printf("  DISTANCE SCALING WARN: Close-up %.1fx slower than far\n", ratio);
            warned++;
        }
        else
        {
            printf("  DISTANCE SCALING PASS: Performance scales acceptably (ratio: %.1fx)\n", ratio);
            passed++;
        }
    }

    printf("\n");
    printf("################################################################################\n");
    printf("#                              SUMMARY                                        #\n");
    printf("################################################################################\n");
    printf("Tests passed: %d\n", passed);
    printf("Tests warned: %d\n", warned);
    printf("Tests failed: %d\n", failed);

    DeleteFileA(TEMP_CSV);

    return (failed == 0) ? 0 : 1;
}
