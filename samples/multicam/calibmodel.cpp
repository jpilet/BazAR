#include "calibmodel.h"

CalibModel::CalibModel(const char *modelfile)
	: modelfile (modelfile)
{
	image=0;
	win = "BazAR";
}

CalibModel::~CalibModel() {
	if (image) cvReleaseImage(&image);
}

CalibModel *objectPtr=0;

void CalibModel::onMouse(int event, int x, int y, int flags)
{
	if (event == CV_EVENT_LBUTTONDOWN) {
		// try to grab something
		grab = -1;
		for (int i=0; i<4; i++) {
			int dx = x-corners[i].x;
			int dy = y-corners[i].y;
			if (sqrt((double)(dx*dx+dy*dy)) <10) {
				grab = i;
				break;
			}
		}
	}
	
	if (grab!=-1) {
		corners[grab].x = x;
		corners[grab].y = y;
	}

	if (event == CV_EVENT_LBUTTONUP) {
		grab=-1;
	}
}

bool CalibModel::buildCached(int nbcam, CvCapture *capture, bool cache, planar_object_recognizer &detector)
{

	detector.ransac_dist_threshold = 5;
	detector.max_ransac_iterations = 800;
	detector.non_linear_refine_threshold = 1.5;

	// A lower threshold will allow detection in harder conditions, but
	// might lead to false positives.
	detector.match_score_threshold=.03f;

	detector.min_view_rate=.1;
	detector.views_number = 100;

	// Should we train or load the classifier ?
	if(cache && detector.build_with_cache(
				string(modelfile), // mode image file name
				500,               // maximum number of keypoints on the model
				32,                // patch size in pixels
				3,                 // yape radius. Use 3,5 or 7.
				16,                // number of trees for the classifier. Somewhere between 12-50
				3                  // number of levels in the gaussian pyramid
				))
	{
		// loading worked. Remember the region of interest.
		corners[0].x = detector.new_images_generator.u_corner1;
		corners[0].y = detector.new_images_generator.v_corner1;
		corners[1].x = detector.new_images_generator.u_corner2;
		corners[1].y = detector.new_images_generator.v_corner2;
		corners[2].x = detector.new_images_generator.u_corner3;
		corners[2].y = detector.new_images_generator.v_corner3;
		corners[3].x = detector.new_images_generator.u_corner4;
		corners[3].y = detector.new_images_generator.v_corner4;
		image = cvLoadImage(modelfile, cvQueryFrame(capture)->nChannels == 3);
	} else {
		// ask the user the take a shot of the model
		if (!interactiveSetup(capture)) return false;

		// train the classifier to detect this model
		if (!detector.build(image, 400, 32, 3, 16, 3,0, 0))
			return false;

		// save the image
		if (!cvSaveImage(modelfile, image)) return false;

		// and the region of interest (ROI)
		string roifn = string(modelfile) + ".roi";
		ofstream roif(roifn.c_str());
		if (!roif.good()) return false;
		for (int i=0;i<4; i++) 
			roif << corners[i].x << " " << corners[i].y << "\n";
		roif.close();

		// and the trained classifier
		detector.save(string(modelfile)+".classifier");
	}

	float cn[4][2];
	for (int i=0; i<4; i++) {
		cn[i][0] = corners[i].x;
		cn[i][1] = corners[i].y;
	}

	// prepare the light calibration reference
	return map.init(nbcam, image, cn, 8, 6);
}

static void putText(IplImage *im, const char *text, CvPoint p, CvFont *f1, CvFont *f2)
{
	cvPutText(im,text,p,f2, cvScalarAll(0));
	cvPutText(im,text,p,f1, cvScalar(0,255, 255));
}


IplImage *myRetrieveFrame(CvCapture *capture)
{
	static IplImage *s=0;
	IplImage *frame =cvRetrieveFrame(capture);
	if (frame == 0) return 0;
	IplImage *ret=frame;
	if (frame->nChannels==1) {
		if (!s) s=cvCreateImage(cvSize(frame->width,frame->height), IPL_DEPTH_8U, 3);
		cvCvtColor(frame,s,CV_GRAY2BGR);
		ret = s;
	}
	if (ret->origin) {
		if (!s) s=cvCreateImage(cvSize(frame->width,frame->height), IPL_DEPTH_8U, 3);
		cvFlip(ret, s);
		ret->origin=0;
		ret = s;
	}
	return ret;
}
IplImage *myQueryFrame(CvCapture *capture)
{
	cvGrabFrame(capture);
	return myRetrieveFrame(capture);
}



bool CalibModel::interactiveSetup(CvCapture *capture)
{

	CvFont font, fontbold;

	cvInitFont( &font, CV_FONT_HERSHEY_PLAIN, 1, 1);
	cvInitFont( &fontbold, CV_FONT_HERSHEY_PLAIN, 1, 1, 0, 5);

	cvNamedWindow(win, CV_WINDOW_AUTOSIZE);
	grab=-1;
	
	objectPtr = this;
	cvSetMouseCallback(win, onMouseStatic, this);

	bool pause=false;
	IplImage *frame = myQueryFrame(capture);
	IplImage *shot=0, *text=0;

	state = TAKE_SHOT;

	bool accepted =false;
	while (!accepted) {

		// wait for a key
		char k = cvWaitKey(10);

		if (k==27 || k=='q') {
			if (shot) cvReleaseImage(&shot);
			if (text) cvReleaseImage(&text);
			return false;
		}

		// clear text or grab the image to display
		if (!pause || shot==0) {
			frame = myQueryFrame(capture);
			if (!text) {
				text=cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 3);
				int d = 30;
				corners[0].x = d;
				corners[0].y = d;
				corners[1].x = frame->width-d;
				corners[1].y = d;
				corners[2].x = frame->width-d;
				corners[2].y = frame->height-d;
				corners[3].x = d;
				corners[3].y = frame->height-d;
			} 
			if (frame->nChannels==1)
				cvCvtColor(frame, text, CV_GRAY2BGR);
			else 
				cvCopy(frame,text);
		} else {
			if (shot->nChannels==1)
				cvCvtColor(shot, text, CV_GRAY2BGR);
			else
				cvCopy(shot, text);
		}

		// display text / react to keyboard
		switch (state) {
			default:
			case TAKE_SHOT:
				if (k==' ') {
					if (shot) cvCopy(frame,shot);
					else shot = cvCloneImage(frame);
					pause = true;
					state = CORNERS;
					k=-1;
				} else {
					putText(text,"Please take a frontal view", cvPoint(3,20), &font, &fontbold);
					putText(text,"of a textured planar surface", cvPoint(3,40), &font, &fontbold);
					putText(text,"and press space", cvPoint(3,60), &font, &fontbold);
					break;
				}
			case CORNERS:
					putText(text, "Drag corners to match the", cvPoint(3,20), &font, &fontbold);
					putText(text, "calibration target", cvPoint(3,40), &font, &fontbold);
					putText(text, "press 'r' to restart", cvPoint(3,60), &font, &fontbold);
					putText(text, "press space when ready", cvPoint(3,80), &font, &fontbold);
					if (k=='r') {
						pause = false;
						state = TAKE_SHOT;
					}
					if (k==' ') {
						accepted=true;
					}
					int four = 4;
					CvPoint *ptr = corners;
					cvPolyLine(text, &ptr, &four, 1, 1,
							cvScalar(0,255,0));
					break;
		}
		cvShowImage(win, text);
	}

	cvReleaseImage(&text);
	image = shot;
	return true;
}

void CalibModel::onMouseStatic(int event, int x, int y, int flags, void* param)
{
	if (param)
		((CalibModel *)param)->onMouse(event,x,y,flags);
	if (objectPtr)
		objectPtr->onMouse(event,x,y,flags);
	else
		cerr << "onMouseStatic(): null-pointer.\n";
}


