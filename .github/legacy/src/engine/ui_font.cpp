#include "ui_font.h"

namespace patch {

void font5x7_rows(uint8_t out_rows[7], char c) {
    for (int i = 0; i < 7; i++) out_rows[i] = 0;

    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');

    switch (c) {
        case ' ': return;
        case ':': out_rows[1] = 0x04; out_rows[4] = 0x04; return;
        case '+': out_rows[2] = 0x04; out_rows[3] = 0x1F; out_rows[4] = 0x04; return;
        case '-': out_rows[3] = 0x1F; return;
        case '.': out_rows[6] = 0x04; return;
        case '/': out_rows[0] = 0x01; out_rows[1] = 0x02; out_rows[2] = 0x04; out_rows[3] = 0x08; out_rows[4] = 0x10; return;

        case '0': out_rows[0]=0x0E; out_rows[1]=0x11; out_rows[2]=0x13; out_rows[3]=0x15; out_rows[4]=0x19; out_rows[5]=0x11; out_rows[6]=0x0E; return;
        case '1': out_rows[0]=0x04; out_rows[1]=0x0C; out_rows[2]=0x04; out_rows[3]=0x04; out_rows[4]=0x04; out_rows[5]=0x04; out_rows[6]=0x0E; return;
        case '2': out_rows[0]=0x0E; out_rows[1]=0x11; out_rows[2]=0x01; out_rows[3]=0x06; out_rows[4]=0x08; out_rows[5]=0x10; out_rows[6]=0x1F; return;
        case '3': out_rows[0]=0x0E; out_rows[1]=0x11; out_rows[2]=0x01; out_rows[3]=0x06; out_rows[4]=0x01; out_rows[5]=0x11; out_rows[6]=0x0E; return;
        case '4': out_rows[0]=0x02; out_rows[1]=0x06; out_rows[2]=0x0A; out_rows[3]=0x12; out_rows[4]=0x1F; out_rows[5]=0x02; out_rows[6]=0x02; return;
        case '5': out_rows[0]=0x1F; out_rows[1]=0x10; out_rows[2]=0x1E; out_rows[3]=0x01; out_rows[4]=0x01; out_rows[5]=0x11; out_rows[6]=0x0E; return;
        case '6': out_rows[0]=0x06; out_rows[1]=0x08; out_rows[2]=0x10; out_rows[3]=0x1E; out_rows[4]=0x11; out_rows[5]=0x11; out_rows[6]=0x0E; return;
        case '7': out_rows[0]=0x1F; out_rows[1]=0x01; out_rows[2]=0x02; out_rows[3]=0x04; out_rows[4]=0x08; out_rows[5]=0x08; out_rows[6]=0x08; return;
        case '8': out_rows[0]=0x0E; out_rows[1]=0x11; out_rows[2]=0x11; out_rows[3]=0x0E; out_rows[4]=0x11; out_rows[5]=0x11; out_rows[6]=0x0E; return;
        case '9': out_rows[0]=0x0E; out_rows[1]=0x11; out_rows[2]=0x11; out_rows[3]=0x0F; out_rows[4]=0x01; out_rows[5]=0x02; out_rows[6]=0x0C; return;

        case 'A': out_rows[0]=0x0E; out_rows[1]=0x11; out_rows[2]=0x11; out_rows[3]=0x1F; out_rows[4]=0x11; out_rows[5]=0x11; out_rows[6]=0x11; return;
        case 'B': out_rows[0]=0x1E; out_rows[1]=0x11; out_rows[2]=0x11; out_rows[3]=0x1E; out_rows[4]=0x11; out_rows[5]=0x11; out_rows[6]=0x1E; return;
        case 'C': out_rows[0]=0x0E; out_rows[1]=0x11; out_rows[2]=0x10; out_rows[3]=0x10; out_rows[4]=0x10; out_rows[5]=0x11; out_rows[6]=0x0E; return;
        case 'D': out_rows[0]=0x1E; out_rows[1]=0x11; out_rows[2]=0x11; out_rows[3]=0x11; out_rows[4]=0x11; out_rows[5]=0x11; out_rows[6]=0x1E; return;
        case 'E': out_rows[0]=0x1F; out_rows[1]=0x10; out_rows[2]=0x10; out_rows[3]=0x1E; out_rows[4]=0x10; out_rows[5]=0x10; out_rows[6]=0x1F; return;
        case 'F': out_rows[0]=0x1F; out_rows[1]=0x10; out_rows[2]=0x10; out_rows[3]=0x1E; out_rows[4]=0x10; out_rows[5]=0x10; out_rows[6]=0x10; return;
        case 'G': out_rows[0]=0x0E; out_rows[1]=0x11; out_rows[2]=0x10; out_rows[3]=0x17; out_rows[4]=0x11; out_rows[5]=0x11; out_rows[6]=0x0E; return;
        case 'H': out_rows[0]=0x11; out_rows[1]=0x11; out_rows[2]=0x11; out_rows[3]=0x1F; out_rows[4]=0x11; out_rows[5]=0x11; out_rows[6]=0x11; return;
        case 'I': out_rows[0]=0x0E; out_rows[1]=0x04; out_rows[2]=0x04; out_rows[3]=0x04; out_rows[4]=0x04; out_rows[5]=0x04; out_rows[6]=0x0E; return;
        case 'J': out_rows[0]=0x01; out_rows[1]=0x01; out_rows[2]=0x01; out_rows[3]=0x01; out_rows[4]=0x11; out_rows[5]=0x11; out_rows[6]=0x0E; return;
        case 'K': out_rows[0]=0x11; out_rows[1]=0x12; out_rows[2]=0x14; out_rows[3]=0x18; out_rows[4]=0x14; out_rows[5]=0x12; out_rows[6]=0x11; return;
        case 'L': out_rows[0]=0x10; out_rows[1]=0x10; out_rows[2]=0x10; out_rows[3]=0x10; out_rows[4]=0x10; out_rows[5]=0x10; out_rows[6]=0x1F; return;
        case 'M': out_rows[0]=0x11; out_rows[1]=0x1B; out_rows[2]=0x15; out_rows[3]=0x11; out_rows[4]=0x11; out_rows[5]=0x11; out_rows[6]=0x11; return;
        case 'N': out_rows[0]=0x11; out_rows[1]=0x19; out_rows[2]=0x15; out_rows[3]=0x13; out_rows[4]=0x11; out_rows[5]=0x11; out_rows[6]=0x11; return;
        case 'O': out_rows[0]=0x0E; out_rows[1]=0x11; out_rows[2]=0x11; out_rows[3]=0x11; out_rows[4]=0x11; out_rows[5]=0x11; out_rows[6]=0x0E; return;
        case 'P': out_rows[0]=0x1E; out_rows[1]=0x11; out_rows[2]=0x11; out_rows[3]=0x1E; out_rows[4]=0x10; out_rows[5]=0x10; out_rows[6]=0x10; return;
        case 'Q': out_rows[0]=0x0E; out_rows[1]=0x11; out_rows[2]=0x11; out_rows[3]=0x11; out_rows[4]=0x15; out_rows[5]=0x12; out_rows[6]=0x0D; return;
        case 'R': out_rows[0]=0x1E; out_rows[1]=0x11; out_rows[2]=0x11; out_rows[3]=0x1E; out_rows[4]=0x14; out_rows[5]=0x12; out_rows[6]=0x11; return;
        case 'S': out_rows[0]=0x0F; out_rows[1]=0x10; out_rows[2]=0x10; out_rows[3]=0x0E; out_rows[4]=0x01; out_rows[5]=0x01; out_rows[6]=0x1E; return;
        case 'T': out_rows[0]=0x1F; out_rows[1]=0x04; out_rows[2]=0x04; out_rows[3]=0x04; out_rows[4]=0x04; out_rows[5]=0x04; out_rows[6]=0x04; return;
        case 'U': out_rows[0]=0x11; out_rows[1]=0x11; out_rows[2]=0x11; out_rows[3]=0x11; out_rows[4]=0x11; out_rows[5]=0x11; out_rows[6]=0x0E; return;
        case 'V': out_rows[0]=0x11; out_rows[1]=0x11; out_rows[2]=0x11; out_rows[3]=0x11; out_rows[4]=0x11; out_rows[5]=0x0A; out_rows[6]=0x04; return;
        case 'W': out_rows[0]=0x11; out_rows[1]=0x11; out_rows[2]=0x11; out_rows[3]=0x11; out_rows[4]=0x15; out_rows[5]=0x1B; out_rows[6]=0x11; return;
        case 'X': out_rows[0]=0x11; out_rows[1]=0x11; out_rows[2]=0x0A; out_rows[3]=0x04; out_rows[4]=0x0A; out_rows[5]=0x11; out_rows[6]=0x11; return;
        case 'Y': out_rows[0]=0x11; out_rows[1]=0x11; out_rows[2]=0x0A; out_rows[3]=0x04; out_rows[4]=0x04; out_rows[5]=0x04; out_rows[6]=0x04; return;
        case 'Z': out_rows[0]=0x1F; out_rows[1]=0x01; out_rows[2]=0x02; out_rows[3]=0x04; out_rows[4]=0x08; out_rows[5]=0x10; out_rows[6]=0x1F; return;
        default: return;
    }
}

}
