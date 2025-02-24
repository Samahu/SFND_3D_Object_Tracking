#include <numeric>
#include "matching2D.hpp"

using namespace std;

// Find best matches for keypoints in two camera images based on several matching methods
void matchDescriptors(std::vector<cv::KeyPoint> &kPtsSource, std::vector<cv::KeyPoint> &kPtsRef, cv::Mat &descSource, cv::Mat &descRef,
                      std::vector<cv::DMatch> &matches, std::string descriptorType, std::string matcherType, std::string selectorType)
{
    // configure matcher
    bool crossCheck = false;
    cv::Ptr<cv::DescriptorMatcher> matcher;

    if (matcherType.compare("MAT_BF") == 0)
    {
        int normType = descriptorType == "DES_BINARY" ?
                        cv::NORM_HAMMING :
                        cv::NORM_L2;
        matcher = cv::BFMatcher::create(normType, crossCheck);
    }
    else if (matcherType.compare("MAT_FLANN") == 0)
    {
        // make sure both as 32f when using flann
        // if (descSource.type() != CV_32F)
        {
            descSource.convertTo(descSource, CV_32F);
            descRef.convertTo(descRef, CV_32F);
        }
        matcher = cv::FlannBasedMatcher::create();
    }

    // perform matching task
    if (selectorType.compare("SEL_NN") == 0)
    { // nearest neighbor (best match)
        matcher->match(descSource, descRef, matches); // Finds the best match for each descriptor in desc1
    }
    else if (selectorType.compare("SEL_KNN") == 0)
    { // k nearest neighbors (k=2)
        std::vector< std::vector<cv::DMatch> > knn_matches;
        if(kPtsSource.size() > 2 && kPtsRef.size() > 2) {   // only consider the case where we have more than 2 kpts
            matcher->knnMatch(descSource, descRef, knn_matches, 2);

            const float ratio_thresh = 0.8f;
            for (size_t i = 0; i < knn_matches.size(); ++i)
            {
                if (knn_matches[i][0].distance < ratio_thresh * knn_matches[i][1].distance) {
                    matches.push_back(knn_matches[i][0]);
                }
            }
        }
    }
}

// Use one of several types of state-of-art descriptors to uniquely identify keypoints
void descKeypoints(vector<cv::KeyPoint> &keypoints, cv::Mat &img, cv::Mat &descriptors, string descriptorType)
{
    // select appropriate descriptor
    cv::Ptr<cv::DescriptorExtractor> extractor;
    if (descriptorType.compare("BRISK") == 0)
    {

        int threshold = 30;        // FAST/AGAST detection threshold score.
        int octaves = 3;           // detection octaves (use 0 to do single scale)
        float patternScale = 1.0f; // apply this scale to the pattern used for sampling the neighbourhood of a keypoint.

        extractor = cv::BRISK::create(threshold, octaves, patternScale);
    }
    else if (descriptorType.compare("BRIEF") == 0)
    {
        extractor = cv::xfeatures2d::BriefDescriptorExtractor::create();
    }
    else if (descriptorType.compare("ORB") == 0) {
        extractor = cv::ORB::create();
    }
    else if (descriptorType.compare("FREAK") == 0) {
        extractor = cv::xfeatures2d::FREAK::create();
    }
    else if (descriptorType.compare("AKAZE") == 0) {
        extractor = cv::AKAZE::create();
    }    
    else if (descriptorType.compare("SIFT") == 0) {
        extractor = cv::xfeatures2d::SIFT::create();
    }

    // perform feature description
    double t = (double)cv::getTickCount();
    extractor->compute(img, keypoints, descriptors);
    t = ((double)cv::getTickCount() - t) / cv::getTickFrequency();
    // cout << descriptorType << " descriptor extraction in " << 1000 * t / 1.0 << " ms" << endl;
}

// Detect keypoints in image using the traditional Shi-Thomasi detector
void detKeypointsShiTomasi(vector<cv::KeyPoint> &keypoints, cv::Mat &img, bool bVis)
{
    // compute detector parameters based on image size
    int blockSize = 4;       //  size of an average block for computing a derivative covariation matrix over each pixel neighborhood
    double maxOverlap = 0.0; // max. permissible overlap between two features in %
    double minDistance = (1.0 - maxOverlap) * blockSize;
    int maxCorners = img.rows * img.cols / max(1.0, minDistance); // max. num. of keypoints

    double qualityLevel = 0.01; // minimal accepted quality of image corners
    double k = 0.04;

    
    vector<cv::Point2f> corners;
    cv::goodFeaturesToTrack(img, corners, maxCorners, qualityLevel, minDistance, cv::Mat(), blockSize, false, k);

    // add corners to result vector
    for (auto it = corners.begin(); it != corners.end(); ++it)
    {
        cv::KeyPoint newKeyPoint;
        newKeyPoint.pt = cv::Point2f((*it).x, (*it).y);
        newKeyPoint.size = blockSize;
        keypoints.push_back(newKeyPoint);
    }

    // cout << "Shi-Tomasi detection with n=" << keypoints.size() << " keypoint" << endl;

    // visualize results
    if (bVis)
    {
        cv::Mat visImage = img.clone();
        cv::drawKeypoints(img, keypoints, visImage, cv::Scalar::all(-1), cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
        string windowName = "Shi-Tomasi Corner Detector Results";
        cv::namedWindow(windowName, 6);
        imshow(windowName, visImage);
        cv::waitKey(0);
    }
}

void detKeypointsHarris(std::vector<cv::KeyPoint> &keypoints, cv::Mat &img, bool bVis) {

    // Parameters for Harris corner detection
    int blockSize = 3; // Neighborhood size for corner detection
    int apertureSize = 3; // Aperture parameter for Sobel operator
    double k = 0.04; // Harris detector free parameter

    // Detect Harris corners
    cv::Mat harrisCorners;
    cv::cornerHarris(img, harrisCorners, blockSize, apertureSize, k);

    // Normalize the result to a 0-255 scale
    cv::normalize(harrisCorners, harrisCorners, 0, 255, cv::NORM_MINMAX, CV_32FC1, cv::Mat());

    // Threshold for marking corners (adjust as needed)
    double threshold = 150.0;
    double maxOverlap = 0.0;

    // Draw circles around the detected corners
    for (int y = 0; y < harrisCorners.rows; y++) {
        for (int x = 0; x < harrisCorners.cols; x++) {
            if (harrisCorners.at<float>(y, x) > threshold) {
                cv::KeyPoint kp;
                kp.pt = cv::Point2f(x, y);
                kp.size = 2 * apertureSize;
                kp.response = harrisCorners.at<float>(y, x);

                // scan all previously added keypoints and perform non-maximum suppression
                bool overlap = false;
                for (auto it = keypoints.begin(); it != keypoints.end(); ++it) {
                    double kptOverlap = cv::KeyPoint::overlap(kp, *it);
                    if (kptOverlap > maxOverlap) {
                        overlap = true;
                        if (kp.response > (*it).response) {
                            *it = kp;
                            break;
                        }
                    }
                }
                if (!overlap) {
                    keypoints.push_back(kp);
                }
            }
        }
    }

    // visualize results
    if (bVis)
    {
        cv::Mat visImage = img.clone();
        cv::drawKeypoints(img, keypoints, visImage, cv::Scalar::all(-1), cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
        string windowName = "Harris Corner Detector Results";
        cv::namedWindow(windowName, 6);
        imshow(windowName, visImage);
        cv::waitKey(0);
    }
}

void detKeypointsModern(std::vector<cv::KeyPoint> &keypoints, cv::Mat &img, std::string detectorType, bool bVis) {
    cv::Ptr<cv::FeatureDetector> detector;
    if (detectorType.compare("FAST") == 0) {
        detector = cv::FastFeatureDetector::create();
    }
    else if (detectorType.compare("BRISK") == 0) {
        detector = cv::BRISK::create();
    }
    else if (detectorType.compare("ORB") == 0) {
        detector = cv::ORB::create();
    }
    else if (detectorType.compare("AKAZE") == 0) {
        detector = cv::AKAZE::create();
    }    
    else if (detectorType.compare("SIFT") == 0) {
        detector = cv::xfeatures2d::SIFT::create();
    }
    detector->detect(img, keypoints);

}