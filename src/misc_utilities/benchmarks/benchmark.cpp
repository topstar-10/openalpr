/*
 * Copyright (c) 2013 New Designs Unlimited, LLC
 * Opensource Automated License Plate Recognition [http://www.openalpr.com]
 *
 * This file is part of OpenAlpr.
 *
 * OpenAlpr is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License
 * version 3 as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

#include <iostream>
#include <fstream>
#include <stdio.h>
#include <sys/stat.h>
#include <numeric>      // std::accumulate

#include "alpr_impl.h"

#include "endtoendtest.h"

#include "support/filesystem.h"

using namespace std;
using namespace cv;

// Given a directory full of lp images (named [statecode]#.png) crop out the alphanumeric characters.
// These will be used to train the OCR

void outputStats(vector<double> datapoints);





int main( int argc, const char** argv )
{
  string country;
  string benchmarkName;
  string inDir;
  string outDir;
  Mat frame;

  //Check if user specify image to process
  if(argc == 5)
  {
    country = argv[1];
    benchmarkName = argv[2];
    inDir = argv[3];
    outDir = argv[4];
  }
  else
  {
    printf("Use:\n\t%s [country] [benchmark name] [img input dir] [results output dir]\n",argv[0]);
    printf("\tex: %s us speed ./speed/usimages ./speed\n",argv[0]);
    printf("\n");
    printf("\ttest names are: speed, segocr, detection\n\n" );
    return 0;
  }

  if (DirectoryExists(inDir.c_str()) == false)
  {
    printf("Input dir does not exist\n");
    return 0;
  }
  if (DirectoryExists(outDir.c_str()) == false)
  {
    printf("Output dir does not exist\n");
    return 0;
  }

  vector<string> files = getFilesInDir(inDir.c_str());
  sort( files.begin(), files.end(), stringCompare );

  if (benchmarkName.compare("segocr") == 0)
  {
    Config* config = new Config(country);
    config->debugOff();

    OCR* ocr = new OCR(config);

    for (int i = 0; i< files.size(); i++)
    {
      if (hasEnding(files[i], ".png") || hasEnding(files[i], ".jpg"))
      {
        string fullpath = inDir + "/" + files[i];

        frame = imread( fullpath.c_str() );
        resize(frame, frame, Size(config->ocrImageWidthPx, config->ocrImageHeightPx));

        Rect plateCoords;
        plateCoords.x = 0;
        plateCoords.y = 0;
        plateCoords.width = frame.cols;
        plateCoords.height = frame.rows;
	
	PipelineData pipeline_data(frame, plateCoords, config);

        char statecode[3];
        statecode[0] = files[i][0];
        statecode[1] = files[i][1];
        statecode[2] = '\0';
        string statecodestr(statecode);

        CharacterRegion charRegion(&pipeline_data);

        if (abs(charRegion.getTopLine().angle) > 4)
        {
          // Rotate image:
          Mat rotated(frame.size(), frame.type());
          Mat rot_mat( 2, 3, CV_32FC1 );
          Point center = Point( frame.cols/2, frame.rows/2 );

          rot_mat = getRotationMatrix2D( center, charRegion.getTopLine().angle, 1.0 );
          warpAffine( frame, rotated, rot_mat, frame.size() );

          rotated.copyTo(frame);
	  pipeline_data.crop_gray = frame;
        }

        CharacterSegmenter charSegmenter(&pipeline_data);
        ocr->performOCR(&pipeline_data);
        ocr->postProcessor->analyze(statecode, 25);

        cout << files[i] << "," << statecode << "," << ocr->postProcessor->bestChars << endl;

        imshow("Current LP", frame);
        waitKey(5);
      }
    }

    delete config;
    delete ocr;
  }
  else if (benchmarkName.compare("detection") == 0)
  {
    Config config(country);
    RegionDetector plateDetector(&config);

    for (int i = 0; i< files.size(); i++)
    {
      if (hasEnding(files[i], ".png") || hasEnding(files[i], ".jpg"))
      {
        string fullpath = inDir + "/" + files[i];
        frame = imread( fullpath.c_str() );

        vector<PlateRegion> regions = plateDetector.detect(frame);

        imshow("Current LP", frame);
        waitKey(5);
      }
    }
  }
  else if (benchmarkName.compare("speed") == 0)
  {
    // Benchmarks speed of region detection, plate analysis, and OCR

    timespec startTime;
    timespec endTime;

    Config config(country);
    config.debugOff();

    AlprImpl alpr(country);
    alpr.config->debugOff();
    alpr.setDetectRegion(true);

    RegionDetector plateDetector(&config);
    StateIdentifier stateIdentifier(&config);
    OCR ocr(&config);

    vector<double> endToEndTimes;
    vector<double> regionDetectionTimes;
    vector<double> stateIdTimes;
    vector<double> lpAnalysisPositiveTimes;
    vector<double> lpAnalysisNegativeTimes;
    vector<double> ocrTimes;
    vector<double> postProcessTimes;

    for (int i = 0; i< files.size(); i++)
    {
      if (hasEnding(files[i], ".png") || hasEnding(files[i], ".jpg"))
      {
        cout << "Image: " << files[i] << endl;

        string fullpath = inDir + "/" + files[i];
        frame = imread( fullpath.c_str() );

        getTime(&startTime);
        vector<Rect> regionsOfInterest;
        regionsOfInterest.push_back(Rect(0, 0, frame.cols, frame.rows));
        alpr.recognize(frame, regionsOfInterest);
        getTime(&endTime);
        double endToEndTime = diffclock(startTime, endTime);
        cout << " -- End to End recognition time: " << endToEndTime << "ms." << endl;
        endToEndTimes.push_back(endToEndTime);

        getTime(&startTime);
        vector<PlateRegion> regions = plateDetector.detect(frame);
        getTime(&endTime);

        double regionDetectionTime = diffclock(startTime, endTime);
        cout << " -- Region detection time: " << regionDetectionTime << "ms." << endl;
        regionDetectionTimes.push_back(regionDetectionTime);

        for (int z = 0; z < regions.size(); z++)
        {
	  
	  PipelineData pipeline_data(frame, regions[z].rect, &config);
	  
          getTime(&startTime);
	  
          stateIdentifier.recognize(&pipeline_data);
          getTime(&endTime);
          double stateidTime = diffclock(startTime, endTime);
          cout << "\tRegion " << z << ": State ID time: " << stateidTime << "ms." << endl;
          stateIdTimes.push_back(stateidTime);

          getTime(&startTime);
          LicensePlateCandidate lp(&pipeline_data);
          lp.recognize();
          getTime(&endTime);
          double analysisTime = diffclock(startTime, endTime);
          cout << "\tRegion " << z << ": Analysis time: " << analysisTime << "ms." << endl;

          if (pipeline_data.plate_area_confidence > 10)
          {
            lpAnalysisPositiveTimes.push_back(analysisTime);

            getTime(&startTime);
            ocr.performOCR(&pipeline_data);
            getTime(&endTime);
            double ocrTime = diffclock(startTime, endTime);
            cout << "\tRegion " << z << ": OCR time: " << ocrTime << "ms." << endl;
            ocrTimes.push_back(ocrTime);

            getTime(&startTime);
            ocr.postProcessor->analyze("", 25);
            getTime(&endTime);
            double postProcessTime = diffclock(startTime, endTime);
            cout << "\tRegion " << z << ": PostProcess time: " << postProcessTime << "ms." << endl;
            postProcessTimes.push_back(postProcessTime);
          }
          else
          {
            lpAnalysisNegativeTimes.push_back(analysisTime);
          }
        }

        waitKey(5);
      }
    }

    cout << endl << "---------------------" << endl;

    cout << "End to End Time Statistics:" << endl;
    outputStats(endToEndTimes);
    cout << endl;

    cout << "Region Detection Time Statistics:" << endl;
    outputStats(regionDetectionTimes);
    cout << endl;

    cout << "State ID Time Statistics:" << endl;
    outputStats(stateIdTimes);
    cout << endl;

    cout << "Positive Region Analysis Time Statistics:" << endl;
    outputStats(lpAnalysisPositiveTimes);
    cout << endl;

    cout << "Negative Region Analysis Time Statistics:" << endl;
    outputStats(lpAnalysisNegativeTimes);
    cout << endl;

    cout << "OCR Time Statistics:" << endl;
    outputStats(ocrTimes);
    cout << endl;

    cout << "Post Processing Time Statistics:" << endl;
    outputStats(postProcessTimes);
    cout << endl;
  }
  else if (benchmarkName.compare("endtoend") == 0)
  {
    EndToEndTest e2eTest(inDir, outDir);
    e2eTest.runTest(country, files);
    
  }
}

void outputStats(vector<double> datapoints)
{
  double sum = std::accumulate(datapoints.begin(), datapoints.end(), 0.0);
  double mean = sum / datapoints.size();

  std::vector<double> diff(datapoints.size());
  std::transform(datapoints.begin(), datapoints.end(), diff.begin(),
                 std::bind2nd(std::minus<double>(), mean));
  double sq_sum = std::inner_product(diff.begin(), diff.end(), diff.begin(), 0.0);
  double stdev = std::sqrt(sq_sum / datapoints.size());

  cout << "\t" << datapoints.size() << " samples, avg: " << mean << "ms,  stdev: " << stdev << endl;
}
