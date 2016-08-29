/*
 *    Copyright (C) 2016 by YOUR NAME HERE
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
SpecificWorker::SpecificWorker(MapPrx& mprx) : GenericWorker(mprx)
{
}

/**
* \brief Default destructor
*/
SpecificWorker::~SpecificWorker()
{
	
}

bool SpecificWorker::setParams(RoboCompCommonBehavior::ParameterList params)
{
	RoboCompCommonBehavior::Parameter par;
	try
	{
		par = params.at("InnerModel");
		if( QFile::exists(QString::fromStdString(par.value)) )
			innerModel = new InnerModel(par.value);
		else
		{
			std::cout << "Innermodel path " << par.value << " not found. "; 
			qFatal("Abort");
		}
	}
	catch(std::exception e)
	{
		qFatal("Error reading config params %s", par.value.c_str());
	}
	
  innerModel->print();
	
	InnerModelRGBD *rgbdNode;
	try{ innerModel->get(rgbdNode, "rgbd"); }
	catch(const std::exception &e){ std::cout << e.what() << std::endl;}

	qDebug() << __FUNCTION__ << rgbdNode->focal;
	timer.start(Period);
	return true;
}

void SpecificWorker::compute()
{
	//qDebug() << __FUNCTION__ << "hola";
}







