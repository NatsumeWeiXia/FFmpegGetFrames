#include <stdlib.h>
#include <string>
#include <cstring>
#include <dlfcn.h>
#include <sstream>
#include <pthread.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/mem.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/pixfmt.h>

#include "com_fiberhome_mediasearch_core_natives_CVideoFrameUtil.h"
}

using std::string;
using std::ostringstream;
using namespace::std;

typedef void(*AV_REGISTER_ALL)(void);

jstring str2jstring(JNIEnv* env, string str)
{
	char* pat = (char*)str.data();
	jclass strClass = (env)->FindClass("Ljava/lang/String;");
	jmethodID ctorID = (env)->GetMethodID(strClass, "<init>", "([BLjava/lang/String;)V");
	jbyteArray bytes = (env)->NewByteArray(strlen(pat));
	(env)->SetByteArrayRegion(bytes, 0, strlen(pat), (jbyte*)pat);
	jstring encoding = (env)->NewStringUTF("UTF-8");
	return (jstring)(env)->NewObject(strClass, ctorID, bytes, encoding);
}

char* jstring2str(JNIEnv* env, jstring jstr)
{
	char* rtn = NULL;
	jclass clsstring = env->FindClass("java/lang/String");
	jstring strencode = env->NewStringUTF("UTF-8");
	jmethodID mid = env->GetMethodID(clsstring, "getBytes", "(Ljava/lang/String;)[B");
	jbyteArray barr = (jbyteArray)env->CallObjectMethod(jstr, mid, strencode);
	jsize alen = env->GetArrayLength(barr);
	jbyte* ba = env->GetByteArrayElements(barr, JNI_FALSE);

	if (alen > 0)
	{
		rtn = (char*)malloc(alen + 1);
		memcpy(rtn, ba, alen);
		rtn[alen] = 0;
	}

	env->ReleaseByteArrayElements(barr, ba, 0);

	return rtn;
}

int EncodeYUVToJPEG(AVFrame* pictureFrame, const char* OutputFileName, int in_w, int in_h)
{		
	//Method 2. More simple
	AVFormatContext *pFormatCtx = avformat_alloc_context();
	avformat_alloc_output_context2(&pFormatCtx, NULL, NULL, OutputFileName);
	AVOutputFormat *fmt = pFormatCtx->oformat;

	AVStream *video_st = avformat_new_stream(pFormatCtx, 0);
	if (video_st == NULL)
	{
		return -1;
	}

	// 获取编解码器上下文信息
	AVCodecContext* pCodecCtx = avcodec_alloc_context3(NULL);
	//pCodecCtx->thread_count = 1;
	//pCodecCtx->thread_type = FF_THREAD_FRAME;

	if (avcodec_parameters_to_context(pCodecCtx, video_st->codecpar) < 0)
	{
		printf("Copy stream failed!");
		return -1;
	}

	pCodecCtx->codec_id = fmt->video_codec;
	pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	pCodecCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;
	pCodecCtx->width = pictureFrame->width;
	pCodecCtx->height = pictureFrame->height;
	pCodecCtx->time_base.num = 1;
	pCodecCtx->time_base.den = 25;
	pCodecCtx->gop_size = 1;
	pCodecCtx->max_b_frames = 0;

	AVDictionary *param = 0;

	av_dict_set(&param, "profile:v", "baseline", 0);
	av_dict_set(&param, "preset", "ultrafast", 0);
	av_dict_set(&param, "tune", "zerolatency", 0);

	av_dump_format(pFormatCtx, 0, OutputFileName, 1);

	AVCodec *pCodec = avcodec_find_encoder(pCodecCtx->codec_id);

	if (!pCodec)
	{
		printf("Codec not found.");
		return -1;
	}

	if (avcodec_open2(pCodecCtx, pCodec, &param) < 0)
	{
		printf("Could not open codec.");
		return -1;
	}

	avformat_write_header(pFormatCtx, NULL);


	int y_size = pCodecCtx->width * pCodecCtx->height;
	AVPacket *pkt = av_packet_alloc();
	av_new_packet(pkt, y_size * 3);


	int ret = avcodec_send_frame(pCodecCtx, pictureFrame);
	while (ret >= 0)
	{
		pkt->stream_index = video_st->index;
		ret = avcodec_receive_packet(pCodecCtx, pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
		{
			return -1;
		}
		else if (ret < 0)
		{
			fprintf(stderr, "Error during encoding\n");
			exit(1);
		}
		av_write_frame(pFormatCtx, pkt);
	}

	av_packet_unref(pkt);
    av_write_trailer(pFormatCtx);

    printf("Encode Successful.\n");

    if (video_st)
    {
        av_free(pictureFrame);
    }
    avformat_free_context(pFormatCtx);
	
	return 0;
}

JNIEXPORT jobject JNICALL Java_com_fiberhome_mediasearch_core_natives_CVideoFrameUtil_getVideoFrame
(JNIEnv* env, jclass cls, jstring filePath, jstring fileTargetPath, jstring fileTargetName, jint grabSize){

	const char *filepath = jstring2str(env, filePath);
	const char *outPath = jstring2str(env, fileTargetPath);
	const char *outVideoName = jstring2str(env, fileTargetName);
	jclass list_jcs = env->FindClass("java/util/ArrayList");
	if (list_jcs == NULL){
		return NULL;
	}
	jmethodID list_init = (env)->GetMethodID(list_jcs, "<init>", "()V");
	jobject list_obj = (env)->NewObject(list_jcs, list_init, "");
	jmethodID list_add = (env)->GetMethodID(list_jcs, "add", "(Ljava/lang/Object;)Z");


	av_register_all();

	avformat_network_init();
	

	int PictureSize;
	int frameFinished;
	uint8_t *buf;


	AVFormatContext *pAVFormatCtx = NULL;
	pAVFormatCtx = avformat_alloc_context();


	//打开文件
	char errorBuf[1024];
	int retOpenFile = avformat_open_input(&pAVFormatCtx, filepath, NULL, NULL);
	if (0 != retOpenFile) {
		av_strerror(retOpenFile, errorBuf, sizeof(errorBuf));
		printf("Couldn't open file %s: %d(%s)\n", filepath, retOpenFile, errorBuf);
		return list_obj;
	}


	//音视频分离
	int retFindStream = avformat_find_stream_info(pAVFormatCtx, NULL);
	if (0 != retFindStream) {
		av_strerror(retFindStream, errorBuf, sizeof(errorBuf));
		printf("Couldn't find stream %s: %d(%s)\n", filepath, retFindStream, errorBuf);
		return list_obj;
	}


	int videoStreamIndex = -1;
	for (int i = 0; i < pAVFormatCtx->nb_streams; i++) {
		AVStream *stream = pAVFormatCtx->streams[i];
		AVCodecParameters *codeParam = stream->codecpar;
		if (AVMEDIA_TYPE_VIDEO == codeParam->codec_type) {
			videoStreamIndex = i;
			break;
		}
	}

	if (-1 == videoStreamIndex) {
		printf("Didn't find a video stream.\n");
		return list_obj;
	}

	//视频流信息
	AVStream *videoStream = pAVFormatCtx->streams[videoStreamIndex];
	AVCodecParameters *codeParam = videoStream->codecpar;
	AVCodecContext *pAVCodeCtx = avcodec_alloc_context3(NULL);
	pAVCodeCtx->thread_count = 8;
	pAVCodeCtx->thread_type = FF_THREAD_FRAME;


	avcodec_parameters_to_context(pAVCodeCtx, codeParam);
	if (0 == pAVCodeCtx) {
		printf("Couldn't create AVCodecContext\n");
		return list_obj;
	}

	//查找视频解码器
	AVCodecID videoCodeId = codeParam->codec_id;
	AVCodec *videoDeCode = avcodec_find_decoder(videoCodeId);
	if (videoDeCode == NULL) {
		printf("Codec not found.\n");
		return list_obj;
	}

	//打开视频解码器
	int retOpenVideoDecode = avcodec_open2(pAVCodeCtx, videoDeCode, NULL);
	if (retOpenVideoDecode != 0) {
		av_strerror(retOpenVideoDecode, errorBuf, sizeof(errorBuf));
		printf("open decode Error. %s\n", errorBuf);
		return list_obj;
	}

	AVPacket *avPacket = av_packet_alloc();
	AVFrame *avVideoFrame = av_frame_alloc();

	AVFrame *pFrameBGR = av_frame_alloc();

	PictureSize = avpicture_get_size(AV_PIX_FMT_BGR24, pAVCodeCtx->width, pAVCodeCtx->height);
	buf = (uint8_t *)av_malloc(PictureSize);

	avpicture_fill((AVPicture *)pFrameBGR, buf, AV_PIX_FMT_BGR24, pAVCodeCtx->width, pAVCodeCtx->height);

	// 获取图像处理上下文
	SwsContext *pSwsCtx = sws_getContext(pAVCodeCtx->width, pAVCodeCtx->height,
		pAVCodeCtx->pix_fmt, pAVCodeCtx->width, pAVCodeCtx->height, AV_PIX_FMT_BGR24,
		SWS_BILINEAR, NULL, NULL, NULL);

	int ret = 0;
	int64_t i = 1;
	//int count = (int)grabSize + 1;

	int64_t durction = pAVFormatCtx->streams[videoStreamIndex]->duration * av_q2d(pAVFormatCtx->streams[videoStreamIndex]->time_base);

	int count = 5;

	std::ostringstream o;
	o << durction;
	jstring durctionStr = str2jstring(env, o.str());
	
	env->CallObjectMethod(list_obj, list_add, durctionStr);

	int64_t postions[3*grabSize];
	int k = 0;

	for(int64_t m = 2; m < 5; m++){
		for(int64_t n = 0; n < (int64_t)grabSize; n++){
			if(durction < count){
				double interval = durction / count / grabSize;
				postions[k] = (m * (durction / count)  + (n * interval) - interval) * AV_TIME_BASE;
			} else {
				int64_t interval = (grabSize - 1)/2;
				postions[k] = (m * (durction / count)  + n - interval) * AV_TIME_BASE;
			}
			k++;

		}

	}

	int start = av_rescale_q(postions[0], AV_TIME_BASE_Q, pAVFormatCtx->streams[videoStreamIndex]->time_base);

	avcodec_flush_buffers(pAVCodeCtx);

	av_seek_frame(pAVFormatCtx, videoStreamIndex, start, AVSEEK_FLAG_ANY);
	
	while (ret >= 0 && i < sizeof(postions)) {

		//从原始数据读取一帧
		ret = av_read_frame(pAVFormatCtx, avPacket);

		
		if (avPacket->stream_index == videoStreamIndex) {


			//送往解码器
			int retPackt = avcodec_send_packet(pAVCodeCtx, avPacket);

			if (retPackt < 0) {
				av_strerror(retPackt, errorBuf, sizeof(errorBuf));
				printf("packet Error. %s\n", errorBuf);
				continue;
			}

			//从解码器获取一帧
			int retDcode = avcodec_receive_frame(pAVCodeCtx, avVideoFrame);

			if (retDcode < 0) {
				av_strerror(retDcode, errorBuf, sizeof(errorBuf));
				continue;
			} else {

				int pts = avVideoFrame->pts * av_q2d(pAVFormatCtx->streams[videoStreamIndex]->time_base);
				int dts = avVideoFrame->pkt_dts * av_q2d(pAVFormatCtx->streams[videoStreamIndex]->time_base);

				sws_scale(pSwsCtx, avVideoFrame->data, avVideoFrame->linesize, 0, pAVCodeCtx->height, pFrameBGR->data, pFrameBGR->linesize);

				
				std::ostringstream o;
				o << outPath;
				o << outVideoName;
				o << i;
				o << ".jpg";
				
				char* fileOutPath = new char[strlen(o.str().c_str()) + 1];
				strcpy(fileOutPath, o.str().c_str());
		
				
				EncodeYUVToJPEG(avVideoFrame, fileOutPath, pAVCodeCtx->width, pAVCodeCtx->height);

				jstring out = env->NewStringUTF(fileOutPath);
				
				env->CallObjectMethod(list_obj, list_add, out);
				
				int64_t pos = postions[i];

				pos = av_rescale_q(pos, AV_TIME_BASE_Q, pAVFormatCtx->streams[videoStreamIndex]->time_base);

				avcodec_flush_buffers(pAVCodeCtx);

				av_seek_frame(pAVFormatCtx, videoStreamIndex, pos, AVSEEK_FLAG_ANY);

				//av_seek_frame(pAVFormatCtx, videoStreamIndex, pos, AVSEEK_FLAG_ANY);

				i++;
			}
		}
	}

	//资源释放
	sws_freeContext(pSwsCtx);
	av_free(pFrameBGR);
	av_frame_free(&avVideoFrame);
	av_packet_free(&avPacket);
	avcodec_close(pAVCodeCtx);
	avformat_close_input(&pAVFormatCtx);
	avformat_network_deinit();

	system("pause");

	return list_obj;
}
