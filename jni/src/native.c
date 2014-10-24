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

#include <SDL.h>
#include <SDL_thread.h>

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#define  LOG_TAG    "VideoDemo"
#define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

#define SDL_AUDIO_BUFFER_SIZE 4096
#define MAX_AUDIO_FRAME_SIZE 192000

typedef struct PacketQueue {
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	SDL_cond *cond;
} PacketQueue;

PacketQueue audioq;

SDL_AudioSpec wanted_spec, spec;

AVDictionary *audioOptionsDict;
int quit = 0;

AVFormatContext *pFormatCtx;
AVCodecContext *pCodecCtx;
AVCodecContext *aCodecCtx;

AVFrame *pFrame;
AVFrame *pFrameRGB;
uint8_t *buffer;
int videoStream, audioStream;

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

		}else if(packet.stream_index==audioStream) {
			packet_queue_put(&audioq, &packet);
		}

		// Free the packet that was allocated by av_read_frame
		av_free_packet(&packet);
	}

	LOGI("packet count %d", i);

	LOGI("ready to free memory resources");
	closeFile();
}

void packet_queue_init(PacketQueue *q) {
  memset(q, 0, sizeof(PacketQueue));
  q->mutex = SDL_CreateMutex();
  q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

  AVPacketList *pkt1;
  if(av_dup_packet(pkt) < 0) {
    return -1;
  }
  pkt1 = av_malloc(sizeof(AVPacketList));
  if (!pkt1)
    return -1;
  pkt1->pkt = *pkt;
  pkt1->next = NULL;


  SDL_LockMutex(q->mutex);

  if (!q->last_pkt)
    q->first_pkt = pkt1;
  else
    q->last_pkt->next = pkt1;
  q->last_pkt = pkt1;
  q->nb_packets++;
  q->size += pkt1->pkt.size;
  SDL_CondSignal(q->cond);

  SDL_UnlockMutex(q->mutex);
  return 0;
}

static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block){
  AVPacketList *pkt1;
  int ret;

  SDL_LockMutex(q->mutex);

  for(;;) {

    if(quit) {
      ret = -1;
      break;
    }

    pkt1 = q->first_pkt;
    if (pkt1) {
      q->first_pkt = pkt1->next;
      if (!q->first_pkt)
	q->last_pkt = NULL;
      q->nb_packets--;
      q->size -= pkt1->pkt.size;
      *pkt = pkt1->pkt;
      av_free(pkt1);
      ret = 1;
      break;
    } else if (!block) {
      ret = 0;
      break;
    } else {
      SDL_CondWait(q->cond, q->mutex);
    }
  }
  SDL_UnlockMutex(q->mutex);
  return ret;
}

int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size) {

  static AVPacket pkt;
  static uint8_t *audio_pkt_data = NULL;
  static int audio_pkt_size = 0;
  static AVFrame frame;

  int len1, data_size = 0;

  for(;;) {
    while(audio_pkt_size > 0) {
      int got_frame = 0;
      len1 = avcodec_decode_audio4(aCodecCtx, &frame, &got_frame, &pkt);
      if(len1 < 0) {
	/* if error, skip frame */
	audio_pkt_size = 0;
	break;
      }
      audio_pkt_data += len1;
      audio_pkt_size -= len1;
      if (got_frame)
      {
          data_size =
            av_samples_get_buffer_size
            (
                NULL,
                aCodecCtx->channels,
                frame.nb_samples,
                aCodecCtx->sample_fmt,
                1
            );
          memcpy(audio_buf, frame.data[0], data_size);
      }
      if(data_size <= 0) {
	/* No data yet, get more frames */
	continue;
      }
      /* We have data, return it and come back for more later */
      return data_size;
    }
    if(pkt.data)
      av_free_packet(&pkt);

    if(quit) {
      return -1;
    }

    if(packet_queue_get(&audioq, &pkt, 1) < 0) {
      return -1;
    }
    audio_pkt_data = pkt.data;
    audio_pkt_size = pkt.size;
  }
}

void audio_callback(void *userdata, Uint8 *stream, int len) {

  AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
  int len1, audio_size;

  static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
  static unsigned int audio_buf_size = 0;
  static unsigned int audio_buf_index = 0;

  while(len > 0) {
    if(audio_buf_index >= audio_buf_size) {
      /* We have already sent all our data; get more */
      audio_size = audio_decode_frame(aCodecCtx, audio_buf, audio_buf_size);
      if(audio_size < 0) {
	/* If error, output silence */
	audio_buf_size = 1024; // arbitrary?
	memset(audio_buf, 0, audio_buf_size);
      } else {
	audio_buf_size = audio_size;
      }
      audio_buf_index = 0;
    }
    len1 = audio_buf_size - audio_buf_index;
    if(len1 > len)
      len1 = len;
    memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
    len -= len1;
    stream += len1;
    audio_buf_index += len1;
  }
}

void openAVfile(){

	av_register_all();

	//SDL init
	//if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
	if(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		LOGE("Could not initialize SDL - %s\n", SDL_GetError());
	    exit(1);
	}

	//if(avformat_open_input(&pFormatCtx, "file:/sdcard/Samsung/Video/Wonders_of_Nature.mp4", NULL, NULL)!=0){
	//if(avformat_open_input(&pFormatCtx, "file:/sdcard/sample_mpeg4.mp4", NULL, NULL)!=0){
	if(avformat_open_input(&pFormatCtx, "file:/sdcard/QuickTime_test5_3m2s_MPEG4ASP_CBR_314kbps_640x480_30fps_AAC-LCv4_CBR_96kbps_Mono_48000Hz.mp4", NULL, NULL)!=0){
		LOGE("Couldn't open file");
		return;
	}

	if(avformat_find_stream_info(pFormatCtx, NULL)<0){
		LOGE("Couldn't find stream information");
		return;
	}

	int i;
	videoStream = -1;
	audioStream = -1;
	for(i=0;i<pFormatCtx->nb_streams;i++){
		if(pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO){
			videoStream = i;
		}
		if(pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && audioStream < 0) {
			audioStream=i;
		}
	}
	if(videoStream == -1){
		LOGE("Didn't find a video stream");
		return;
	}
	if(audioStream == -1){
		LOGE("Didn't find a audio stream");
		return;
	}

	aCodecCtx = pFormatCtx->streams[audioStream]->codec;
	// Set audio settings from codec info
	wanted_spec.freq = aCodecCtx->sample_rate;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = aCodecCtx->channels;
	wanted_spec.silence = 0;
	wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
	wanted_spec.callback = audio_callback;
	wanted_spec.userdata = aCodecCtx;

	if(SDL_OpenAudio(&wanted_spec, &spec) < 0) {
		LOGE("SDL_OpenAudio: %s\n", SDL_GetError());
	    return;
	}
	AVCodec *aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
	if(!aCodec) {
		LOGE("Unsupported codec!\n");
		return;
	}
	if(avcodec_open2(aCodecCtx, aCodec, &audioOptionsDict)<0){
		LOGE("Unable to open audio codec");
	}
	LOGI("audio codec name: %s", aCodecCtx->codec->name);

	// audio_st = pFormatCtx->streams[index]
	packet_queue_init(&audioq);
	SDL_PauseAudio(0);
	LOGI("audio codec checked");

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

/*
int Java_com_weikun_videodemo_MainActivity_nativeInit(JNIEnv * env,
		jobject this) {

	// open default video file
	//openAVfile();

	return 0;
}
*/

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

int main(int argc, char *argv[]) {

  openAVfile();

  //av_strlcpy(is->filename,argv[1],sizeof(is->filename));

  //....
  //av_strlcpy(is->filename,argv[1],sizeof(is->filename));

  //pstrcpy(is->filename, sizeof(is->filename), argv[1]);

  return 0;
}
