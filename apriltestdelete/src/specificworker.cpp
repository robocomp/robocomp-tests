/*
 *    Copyright (C)2020 by YOUR NAME HERE
 *
 *    This file is part of RoboComp
 *
 *    RoboComp is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    RoboComp is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with RoboComp.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "specificworker.h"

/**
* \brief Default constructor
*/
SpecificWorker::SpecificWorker(TuplePrx tprx) : GenericWorker(tprx)
{

}

/**
* \brief Default destructor
*/
SpecificWorker::~SpecificWorker()
{
	std::cout << "Destroying SpecificWorker" << std::endl;
}

bool SpecificWorker::setParams(RoboCompCommonBehavior::ParameterList params)
{
//       THE FOLLOWING IS JUST AN EXAMPLE
//	To use innerModelPath parameter you should uncomment specificmonitor.cpp readConfig method content
//	try
//	{
//		RoboCompCommonBehavior::Parameter par = params.at("InnerModelPath");
//		std::string innermodel_path = par.value;
//		innerModel = new InnerModel(innermodel_path);
//	}
//	catch(const std::exception &e) { qFatal("Error reading config params"); }

	return true;
}

void SpecificWorker::initialize(int period)
{
	std::cout << "Initialize worker" << std::endl;
	this->Period = 100;
	timer.start(Period);
}

void SpecificWorker::compute()
{
	try
	{
		RoboCompCameraSimple::TImage img;
		camerasimple_proxy->getImage(img);

		RoboCompAprilTagsServer::Image april_image;
		april_image.data = img.image;
		april_image.frmt.modeImage = Mode::RGB888Packet;
		april_image.frmt.width = img.width;
		april_image.frmt.height = img.height;
		april_image.frmt.size = 3;
		
		auto tagsList = apriltagsserver_proxy->getAprilTags(april_image, 100, 400,  400);
		
		// cv::Mat image = cv::Mat(img.width, img.height, CV_8UC3, (uchar *)(&img.image[0]));
		// cv::drawMarker(image, cv::Point(img.height/2, img.width/2),  cv::Scalar(0, 0, 255), cv::MARKER_CROSS, 250, 2);
		// cv::imshow("" , image);
		// cv::waitKey(1);

	}
	catch(const Ice::Exception &e)
	{
		std::cout << "Error reading from Camera" << e << std::endl;
	}
}





