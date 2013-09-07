/*
 * native.c
 *
 *  Created on: Aug 17, 2013
 *      Author: weikunlu
 */

#include <jni.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#define  LOG_TAG    "VideoDemo"
#define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

AVFormatContext *pFormatCtx;
AVCodecContext *pCodecCtx;
AVFrame *pFrame;
AVFrame *pFrameRGB;
uint8_t *buffer;
int videoStream;

int stop;

ANativeWindow *native_window; //the android native window where video will be rendered
ANativeWindow_Buffer wbuffer;

void closeFile(){
	// Free the RGB image
	av_free(buffer);
	av_free(pFrameRGB);

	// Free the YUV frame
	av_free(pFrame);

	// Close the codec
	avcodec_close(pCodecCtx);

	// Close the video file
#ifdef _FFMPEG_0_6__
	av_close_input_file(pFormatCtx);
#else
	avformat_close_input(&pFormatCtx);
#endif

}

void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame) {
    FILE *pFile;
    char szFilename[32];
    int y;

    // Open file
    sprintf(szFilename, "frame%d.ppm", iFrame);
    pFile=fopen(szFilename, "wb");
    if(pFile==NULL)
    	return;

    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    // Write pixel data
    for(y=0; y<height; y++)
    	fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width*3, pFile);

    // Close file
    fclose(pFile);
}

void drawAVFrame(){
	struct SwsContext *img_convert_ctx = NULL;
	int frameFinished = 0;
	AVPacket packet;

	int i = 0;
	while(av_read_frame(pFormatCtx, &packet)>=0 && !stop){

		// Is this a packet from the video stream?
		if(packet.stream_index == videoStream){
			// Decode video frame
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

			LOGI("framefinished: %d", frameFinished);
			if(frameFinished){
				// Convert the image from its native format to RGB
				/* img_convert() is deprecated method since FFMPEG 0.5.* builds.
				img_convert((AVPicture *)pFrameRGB, PIX_FMT_RGB24,
				            (AVPicture*)pFrame, pCodecCtx->pix_fmt,
							pCodecCtx->width, pCodecCtx->height);
				*/
				int target_width = ANativeWindow_getWidth(native_window);
				int target_height = ANativeWindow_getHeight(native_window);
				LOGI("window size: %d, %d", target_width, target_height);
				LOGI("media size: %d, %d", pCodecCtx->width, pCodecCtx->height);
				LOGI("frame size: %d, %d", pFrame->width, pFrame->height);
				if(pCodecCtx->pix_fmt != AV_PIX_FMT_NONE)
					LOGI("src pix_fmt: %s", av_get_pix_fmt_name(pCodecCtx->pix_fmt));
				else
					LOGI("src pix_fmt: none");

				img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
						pCodecCtx->width, pCodecCtx->height, PIX_FMT_RGB565,
						SWS_BICUBIC, NULL, NULL, NULL);
				if(img_convert_ctx == NULL){
					LOGE("Couldn't initialize conversion context");
					return;
				}

				sws_scale(img_convert_ctx,
						(const uint8_t* const *)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
						pFrameRGB->data, pFrameRGB->linesize);
				sws_freeContext(img_convert_ctx);
				LOGI("after scale frame size: %d, %d", pFrameRGB->width, pFrameRGB->height);

				LOGI("ready to set buffers");
				ANativeWindow_setBuffersGeometry(native_window, pCodecCtx->width, pCodecCtx->height, WINDOW_FORMAT_RGB_565);

				LOGI("ready to lock native window");
				if (ANativeWindow_lock(native_window, &wbuffer, NULL) < 0) {
					LOGE("Unable to lock window buffer");
					return;
				}

				LOGI("mem copy frame");
				LOGI("linesize = %d linesize1 = %d linesize2 = %d linesize3 = %d ph = %d pw = %d",
										pFrame->linesize[0], pFrame->linesize[1], pFrame->linesize[2], pFrame->linesize[3],
										pFrame->height, pFrame->width);
				memcpy(wbuffer.bits, buffer,  pFrame->width * pFrame->height * 2);

				//memcpy(wbuffer.bits, pFrameRGB->data[0], pFrame->width * pFrame->height);
				//memcpy(wbuffer.bits+pFrame->width*pFrame->height, pFrameRGB->data[1], pFrame->width*pFrame->height>>2);
				//memcpy(wbuffer.bits+(pFrame->width*pFrame->height*5>>2), pFrameRGB->data[2], pFrame->width*pFrame->height>>2);

				//printf("linesize = %d linesize1 = %d linesize2 = %d linesize3 = %dn", pFrame.linesize[0], pFrame.linesize[1], pFrame.linesize[2], pFrame.linesize[3]);
				//memcpy(wbuffer.bits, pFrame->data[0], pFrame->linesize[0] * wbuffer.height);
				//memcpy(wbuffer.bits+picture->linesize[0] * wbuffer.height, picture.data[1], picture->linesize[1] * wbuffer.height/2);
				//memcpy(wbuffer.bits+picture->linesize[0] * wbuffer.height +picture->linesize[1] * wbuffer.height/2, picture->data[2], picture->linesize[2] * wbuffer.height/2);

				//resample_yv12((guchar*)wbuffer.bits,wbuffer.width,wbuffer.height, (guchar*)pdata, pFrame->width, pFrame->height, SCALE_TYEP_NEAREST);
				//get_yuvdata(wbuffer.bits, pFrame);

				LOGI("memcpy done and unlock window");
				ANativeWindow_unlockAndPost(native_window);

				i++;
			}

		}

		// Free the packet that was allocated by av_read_frame
		av_free_packet(&packet);
	}

	LOGI("packet count %d", i);

	LOGI("ready to free memory resources");
	closeFile();
}

void openAVfile(){

	av_register_all();

	if(avformat_open_input(&pFormatCtx, "file:/sdcard/Samsung/Video/Wonders_of_Nature.mp4", NULL, NULL)!=0){
		LOGE("Couldn't open file");
		return;
	}

	if(avformat_find_stream_info(pFormatCtx, NULL)<0){
		LOGE("Couldn't find stream information");
		return;
	}

	int i;
	videoStream = -1;
	for(i=0;i<pFormatCtx->nb_streams;i++){
		if(pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO){
			videoStream = i;
			break;
		}
	}
	if(videoStream == -1){
		LOGE("Didn't find a video stream");
		return;
	}
	pCodecCtx = pFormatCtx->streams[videoStream]->codec;

	AVCodec *pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if(pCodec == NULL){
		LOGE("Unsupported codec");
		return;
	}
	if(avcodec_open2(pCodecCtx, pCodec, NULL)<0){
		LOGE("Unable to open codec");
		return;
	}

	LOGI("codec name: %s", pCodecCtx->codec->name);

	// Allocate video frame
	pFrame = avcodec_alloc_frame();

	// Allocate an AVFrame structure
	pFrameRGB = avcodec_alloc_frame();

	// Determine required buffer size and allocate buffer
	int numBytes = avpicture_get_size(PIX_FMT_RGB565, pCodecCtx->width, pCodecCtx->height);
	buffer = (uint8_t*) av_malloc(numBytes * sizeof(uint8_t));
	LOGI("buffer size: %d", buffer);

	// Assign appropriate parts of buffer to image planes in pFrameRGB
	// Note that pFrameRGB is an AVFrame, but AVFrame is a superset
	// of AVPicture
	avpicture_fill((AVPicture*)pFrameRGB, buffer, PIX_FMT_RGB565, pCodecCtx->width, pCodecCtx->height);

	LOGI("ready to draw frame");
}

void Java_com_weikun_videodemo_MainActivity_nativeVideoPlay(JNIEnv * env, jobject this){
	LOGI("create thread to play video");
	pthread_t decodeThread;
	stop = 0;
	pthread_create(&decodeThread, NULL, (void*)drawAVFrame, NULL);
}

void Java_com_weikun_videodemo_MainActivity_nativeVideoStop(JNIEnv * env, jobject this){
	stop = 1;
}

int Java_com_weikun_videodemo_MainActivity_nativeInit(JNIEnv * env,
		jobject this) {

	// open default video file
	openAVfile();

	return 0;
}

void Java_com_weikun_videodemo_MainActivity_nativeSurfaceInit(JNIEnv *env, jobject thiz, jobject surface) {

	ANativeWindow *new_native_window = ANativeWindow_fromSurface(env, surface);

	LOGI("Received surface %p (native window %p)", surface, new_native_window);

	if (native_window) {
		ANativeWindow_release(native_window);
		if (native_window == new_native_window) {
			LOGI("New native window is the same as the previous one",
					native_window);
			/*if (data->video_sink) {
			 gst_x_overlay_expose(GST_X_OVERLAY(data->video_sink));
			 gst_x_overlay_expose(GST_X_OVERLAY(data->video_sink));
			 }*/
			return;
		} else {
			LOGI("Released previous native window %p", native_window);
			//data->initialized = FALSE;
		}
	}
	native_window = new_native_window;

}

void Java_com_weikun_videodemo_MainActivity_nativeSurfaceFinalize(JNIEnv *env, jobject thiz) {

	LOGI("Releasing Native Window %p", native_window);

	/*if (data->video_sink) {
		gst_x_overlay_set_window_handle(GST_X_OVERLAY(data->video_sink),
				(guintptr) NULL);
		gst_element_set_state(data->pipeline, GST_STATE_READY);
	}*/

	ANativeWindow_release(native_window);
	native_window = NULL;
	//data->initialized = FALSE;
}
