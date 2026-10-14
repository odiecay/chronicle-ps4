#include <libmpeg.h>

int sceMpegInit(void) {
    PS2_UNIMPLEMENTED();
}

int sceMpegCreate(sceMpeg *mp, u_char *work_area, int work_area_size) {
    PS2_UNIMPLEMENTED();
}

int sceMpegDelete(sceMpeg *mp) {
    PS2_UNIMPLEMENTED();
}

int sceMpegReset(sceMpeg *mp) {
    PS2_UNIMPLEMENTED();
}

int sceMpegGetPicture(sceMpeg *mp, sceIpuRGB32 *rgb32, int mbcount) {
    PS2_UNIMPLEMENTED();
}

int sceMpegIsEnd(sceMpeg *mp) {
    PS2_UNIMPLEMENTED();
}

sceMpegCallback sceMpegAddCallback(sceMpeg *mp, sceMpegCbType type, sceMpegCallback callback,
                                   void *anyData) {
    PS2_UNIMPLEMENTED();
}

sceMpegCallback sceMpegAddStrCallback(sceMpeg *mp, sceMpegStrType strType, int ch,
                                      sceMpegCallback callback, void *anyData) {
    PS2_UNIMPLEMENTED();
}

int sceMpegDemuxPssRing(sceMpeg *mp, u_char *start, int size, u_char *buf_start, int buf_size) {
    PS2_UNIMPLEMENTED();
}
