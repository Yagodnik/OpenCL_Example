#include <QCoreApplication>
#include <QDebug>
#include <QTextStream>
#include <QFile>
#include <CL/cl.h>
#include <QElapsedTimer>
#include <QImage>
#include <QColor>
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    AVFormatContext *format = avformat_alloc_context();
    AVCodecContext *ctx;
    AVCodec *codec;
    int video_stream = 0;

    if (avformat_open_input(&format, "/mnt/media/input_video.mp4", NULL, NULL) < 0) {
        qDebug() << "Cant open video!";
        return 1;
    }

    if (avformat_find_stream_info(format, NULL) < 0)
    {
        qDebug() << "cant find streams";
        return 1;
    }

    for(int i = 0;i < format->nb_streams;i++)
    {
        if(format->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream = i;
            break;
        }
    }

    if(video_stream == -1)
    {
        qDebug() << "no video stream";
        return 1;
    }

    ctx = format->streams[video_stream]->codec;
    codec = avcodec_find_decoder(ctx->codec_id);

    if (avcodec_open2(ctx, codec, NULL) < 0)
    {
        qDebug() << "cant open codec " << codec->name;
        return 1;
    }

    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();

    cl_platform_id platform;
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel kernel;

    int err;

    QString kernel_source = "";
    QFile *source_file = new QFile("/mnt/prj/cl_test2/kernel.cl");
    if (!source_file->open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Cant open kernel source!";
        return 1;
    }

    QTextStream stream(source_file->readAll());
    QString line = "";
    while (!stream.atEnd()) {
        line = stream.readLine() + "\n";
        kernel_source += line;
    }

    qDebug() << kernel_source;

    std::string kernel_std = kernel_source.toStdString();
    const char *kernel_source_c = kernel_std.c_str();
    qDebug() << kernel_source_c;

    qDebug() << "platform";
    err = clGetPlatformIDs(1, &platform, NULL);
    if (err != CL_SUCCESS) {
        qDebug() << "Error: Cant get platform id!";
        return 1;
    }

    qDebug() << "device";
    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
    if (err != CL_SUCCESS)
    {
        qDebug() << "Error: Failed to create a device group!";
        return 1;
    }

    qDebug() << "context";
    context = clCreateContext(0, 1, &device, NULL, NULL, NULL);

    qDebug() << "queue";
    queue = clCreateCommandQueue(context, device, 0, NULL);
    if (!queue)
    {
        qDebug() << "Error: Failed to create a command commands";
        return 1;
    }

    qDebug() << "Program 1";
    program = clCreateProgramWithSource(context, 1, &kernel_source_c, NULL, &err);
    if (!program) {
        qDebug() << "Cant create program!";
        qDebug() << err;
        return 1;
    }

    qDebug() << "Building";
    err = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        size_t len;
        char buffer[2048];

        qDebug() << "OpenCL building error!";
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
        qDebug() << buffer;
    }

    qDebug() << "Creating kernel";
    kernel = clCreateKernel(program, "color_convertion", &err);
    if (err < 0) {
        qDebug() << "Kernel creating error " << err;
        return 1;
    }

    int frame_check;

    av_seek_frame(format,
                  video_stream,
                  (1000 / 30 * (double) format->streams[video_stream]->time_base.den /
                                         (double) format->streams[video_stream]->time_base.num),
                  AVSEEK_FLAG_BACKWARD
                  );

    while (true) {
        if (av_read_frame(format, packet) < 0)
        {
            qDebug() << "Cant read frame";
            return 1;
        }

        if (packet->stream_index != video_stream)
        {
            qDebug() << "stream error";
            continue;
        }

        avcodec_decode_video2(ctx, frame, &frame_check, packet);

        if (frame_check)
        {
            cl_mem cl_y_data;
            cl_mem cl_u_data;
            cl_mem cl_v_data;
            cl_mem cl_r_data;
            cl_mem cl_g_data;
            cl_mem cl_b_data;
            cl_mem frame_linesize;
            cl_mem cl_y;
            cl_mem cl_width;

            unsigned char y_ptr[(ctx->height * frame->linesize[0] + ctx->width)];
            unsigned char u_ptr[(ctx->height * frame->linesize[1] + ctx->width)];
            unsigned char v_ptr[(ctx->height * frame->linesize[2] + ctx->width)];

            unsigned char r_ptr[(ctx->height * frame->linesize[0] + ctx->width)];
            unsigned char g_ptr[(ctx->height * frame->linesize[0] + ctx->width)];
            unsigned char b_ptr[(ctx->height * frame->linesize[0] + ctx->width)];

            for (int i = 0;i < (ctx->height * frame->linesize[0] + ctx->width);i++) {
                y_ptr[i] = frame->data[0][i];
            }

            for (int i = 0;i < (ctx->height * frame->linesize[1] + ctx->width);i++) {
                u_ptr[i] = frame->data[1][i];
            }

            for (int i = 0;i < (ctx->height * frame->linesize[2] + ctx->width);i++) {
                v_ptr[i] = frame->data[2][i];
            }


            int y = 0;
            int width = ctx->width;

            qDebug() << sizeof(y_ptr) * ctx->width * ctx->height;
            cl_y_data = clCreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, sizeof(y_ptr), &y_ptr, &err);
            if (err < 0) {
                qDebug() << "Cant create buffer " << err;
                return 1;
            }

            cl_u_data = clCreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, sizeof(u_ptr), &u_ptr, &err);
            if (err < 0) {
                qDebug() << "Cant create buffer " << err;
                return 1;
            }

            cl_v_data = clCreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, sizeof(v_ptr), &v_ptr, &err);
            if (err < 0) {
                qDebug() << "Cant create buffer " << err;
                return 1;
            }

            cl_r_data = clCreateBuffer(context, CL_MEM_COPY_HOST_PTR, sizeof(r_ptr), &r_ptr, &err);
            if (err < 0) {
                qDebug() << "Cant create buffer " << err;
                return 1;
            }

            cl_g_data = clCreateBuffer(context, CL_MEM_COPY_HOST_PTR, sizeof(g_ptr), &g_ptr, &err);
            if (err < 0) {
                qDebug() << "Cant create buffer " << err;
                return 1;
            }

            cl_b_data = clCreateBuffer(context, CL_MEM_COPY_HOST_PTR, sizeof(b_ptr), &b_ptr, &err);
            if (err < 0) {
                qDebug() << "Cant create buffer " << err;
                return 1;
            }

            frame_linesize = clCreateBuffer(context, CL_MEM_COPY_HOST_PTR, sizeof(int) * 8, &frame->linesize, &err);
            if (err < 0) {
                qDebug() << "Cant create buffer " << err;
                return 1;
            }

            cl_y = clCreateBuffer(context, CL_MEM_COPY_HOST_PTR, sizeof(int), &y, &err);
            if (err < 0) {
                qDebug() << "Cant create buffer " << err;
                return 1;
            }

            cl_width = clCreateBuffer(context, CL_MEM_COPY_HOST_PTR, sizeof(int), &width, &err);
            if (err < 0) {
                qDebug() << "Cant create buffer " << err;
                return 1;
            }

            qDebug() << "Argument 0";
            err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &cl_y_data);
            if(err < 0) {
                qDebug() << "Cant set arg 0 " << err;
                return 1;
            }

            qDebug() << "Argument 1";
            err = clSetKernelArg(kernel, 1, sizeof(cl_mem), &cl_u_data);
            if(err < 0) {
                qDebug() << "Cant set arg 1 " << err;
                return 1;
            }

            qDebug() << "Argument 2";
            err = clSetKernelArg(kernel, 2, sizeof(cl_mem), &cl_v_data);
            if(err < 0) {
                qDebug() << "Cant set arg 2 " << err;
                return 1;
            }

            err = clSetKernelArg(kernel, 3, sizeof(cl_mem), &cl_r_data);
            if(err < 0) {
                qDebug() << "Cant set arg 3 " << err;
                return 1;
            }

            qDebug() << "Argument 1";
            err = clSetKernelArg(kernel, 4, sizeof(cl_mem), &cl_g_data);
            if(err < 0) {
                qDebug() << "Cant set arg 4 " << err;
                return 1;
            }

            qDebug() << "Argument 2";
            err = clSetKernelArg(kernel, 5, sizeof(cl_mem), &cl_b_data);
            if(err < 0) {
                qDebug() << "Cant set arg 5 " << err;
                return 1;
            }

            qDebug() << "Argument 3";
            err = clSetKernelArg(kernel, 6, sizeof(cl_mem), &frame_linesize);
            if(err < 0) {
                qDebug() << "Cant set arg 6 " << err;
                return 1;
            }

            qDebug() << "Argument 4";
            err = clSetKernelArg(kernel, 7, sizeof(int), &y);
            if(err < 0) {
                qDebug() << "Cant set arg 7 " << err;
                return 1;
            }

            qDebug() << "Argument 5";
            err = clSetKernelArg(kernel, 8, sizeof(int), &width);
            if(err < 0) {
                qDebug() << "Cant set arg 8 " << err;
                return 1;
            }

            size_t global = ctx->height;
            size_t local = 1;

            qDebug() << "Group info";
            err = clGetKernelWorkGroupInfo(kernel, device, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL);
            if (err != CL_SUCCESS)
            {
                qDebug() << "Error: Failed to retrieve kernel work group info " << err;
                return 1;
            }
            QElapsedTimer timer;
            timer.start();
            qDebug() << "Kernel executing";
            err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global, &local, 0, NULL, NULL);
            if (err)
            {
                qDebug() << "Error: Failed to execute kernel " << err;
                return 1;
            }

            clFinish(queue);

            qDebug() << "Getting result";
            unsigned char *result = (unsigned char *) malloc(sizeof(unsigned char) * ctx->height * width + ctx->width);
            err = clEnqueueReadBuffer(queue, cl_r_data, CL_TRUE, 0, sizeof(unsigned char) * ctx->height * width + ctx->width, result, 0, NULL, NULL);
            if (err != CL_SUCCESS) {
                qDebug() << "Error: Failed on reading buffer " << err;
                return 1;
            }

            unsigned char *result2 = (unsigned char *) malloc(sizeof(unsigned char) * ctx->height * width + ctx->width);
            err = clEnqueueReadBuffer(queue, cl_g_data, CL_TRUE, 0, sizeof(unsigned char) * ctx->height * width + ctx->width, result2, 0, NULL, NULL);
            if (err != CL_SUCCESS) {
                qDebug() << "Error: Failed on reading buffer " << err;
                return 1;
            }

            unsigned char *result3 = (unsigned char *) malloc(sizeof(unsigned char) * ctx->height * width + ctx->width);
            err = clEnqueueReadBuffer(queue, cl_b_data, CL_TRUE, 0, sizeof(unsigned char) * ctx->height * width + ctx->width, result3, 0, NULL, NULL);
            if (err != CL_SUCCESS) {
                qDebug() << "Error: Failed on reading buffer " << err;
                return 1;
            }
            qDebug() << "Time to execute kernel: " << timer.elapsed();

            //QElapsedTimer t1;
            //t1.start();
            QImage image(ctx->width, ctx->height, QImage::Format_RGB888);
            image.fill(qRgb(0, 0, 0));

            for (int yPos = 0; yPos < ctx->height; yPos++) {
                for (int x = 0; x < ctx->width; x++) {
                    const int xLoc = x;
                    const int yLoc = yPos;
                    const int r = result [yPos * width + x];
                    const int g = result2[yPos * width + x];
                    const int b = result3[yPos * width + x];

                    //qDebug() << "R: " << r << " G: " << g << " B: " << b;
                    image.setPixel(x, yPos, qRgb(r, g, b));
                }
            }
            //qDebug() << "Time = " << t1.elapsed();

            image.save("/mnt/media/frame.png");


//            Old mode
            timer.restart();
            for (int y = 0; y < ctx->height; y++) {
                for (int x = 0; x < ctx->width; x++) {
                    const int xx = x >> 1;
                    const int yy = y >> 1;
                    const int Y = frame->data[0][y * frame->linesize[0] + x] - 16;
                    const int U = frame->data[1][yy * frame->linesize[1] + xx] - 128;
                    const int V = frame->data[2][yy * frame->linesize[2] + xx] - 128;
                    const int r = qBound(0, (298 * Y + 409 * V + 128) >> 8, 255);
                    const int g = qBound(0, (298 * Y - 100 * U - 208 * V + 128) >> 8, 255);
                    const int b = qBound(0, (298 * Y + 516 * U + 128) >> 8, 255);

                    image.setPixel(x, y, qRgb(r, g, b));
                }
            }
            qDebug() << "Old mode: " << timer.elapsed();
            image.save("/mnt/media/frame2.png");

            break;
        }
    }

    clReleaseKernel(kernel);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);

    return 0;
}
