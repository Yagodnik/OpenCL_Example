__kernel void color_convertion(__global unsigned char *y_frame_data, __global unsigned char *u_frame_data,  __global unsigned char *v_frame_data, __global unsigned char *r_frame_data, __global unsigned char *g_frame_data,  __global unsigned char *b_frame_data, __global int *frame_linesize, int y, int width) {
    int id = get_global_id(0);
    int yPos = id;

    for (int x = 0; x < width; x++) {
        const int xx = x >> 1;
        const int yy = yPos >> 1;

        const int Y = 1.164 * y_frame_data[(yPos * frame_linesize[0] + x)] - 16;
        const int U = u_frame_data[yy * frame_linesize[1] + xx] - 128;
        const int V = v_frame_data[yy * frame_linesize[2] + xx] - 128;

        const int rT = Y + 1.596 * V;
        const int gT = Y - 0.813 * V - 0.391 * U;
        const int bT = Y + 2.018 * U;

        const int r = clamp(rT, 0, 255);
        const int g = clamp(gT, 0, 255);
        const int b = clamp(bT, 0, 255);

        r_frame_data[yPos * width + x] = r;
        g_frame_data[yPos * width + x] = g;
        b_frame_data[yPos * width + x] = b;
    }
}
