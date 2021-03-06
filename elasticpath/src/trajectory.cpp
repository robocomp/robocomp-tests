/*
 *    Copyright (C)2019 by YOUR NAME HERE
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
#include <cppitertools/range.hpp>
#include <cppitertools/sliding_window.hpp>
#include <cppitertools/zip.hpp>
#include <random>
#include <QGridLayout>
#include <QDesktopWidget>
#include <algorithm>


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

bool SpecificWorker::setParams(RoboCompCommonBehavior::ParameterList _params)
{
	try
	{
		// RoboCompCommonBehavior::Parameter par = params.at("InnerModelPath");
		// std::string innermodel_path = par.value;
		// innerModel = new InnerModel(innermodel_path);
		// InnerModelTransform *parent = innerModel->getTransform("robot");
		// InnerModelTransform *rawOdometryParentNode = innerModel->newTransform("robot_raw_odometry_parent", "static", parent, 0, 0, 0, 0, 0, 0, 0);
		// InnerModelTransform *rawOdometryNode = innerModel->newTransform("robot_raw_odometry", "static", rawOdometryParentNode,  0, 0, 0, 0, 0, 0, 0);
        params = _params;
	}
	catch(const std::exception &e) { qFatal("Error reading config params"); }
	return true;
}

void SpecificWorker::initialize(int period)
{
	std::cout << "Initialize worker" << std::endl;
	resize(QDesktopWidget().availableGeometry(this).size() * 0.6);
	scene.setSceneRect(LEFT, BOTTOM, WIDTH, HEIGHT);
	view.scale( 1, -1 );
	view.setScene(&scene);
	view.setParent(this);
	QGridLayout* layout = new QGridLayout;
    layout->addWidget(&view);
	this->setLayout(layout);
	view.fitInView(scene.sceneRect(), Qt::KeepAspectRatio );
/*	QSettings settings("RoboComp", "ElasticPath");
    settings.beginGroup("MainWindow");
    	resize(settings.value("size", QSize(400, 400)).toSize());
    settings.endGroup();
	settings.beginGroup("QGraphicsView");
		view.setTransform(settings.value("matrix", QTransform()).value<QTransform>());
	settings.endGroup();
*/

    //Load World
    initializeWorld();

	
	// Robot 
	QPolygonF poly2;
	float size = ROBOT_LENGTH/2.f;
	poly2 << QPoint(-size, -size) << QPoint(-size, size) << QPoint(-size/3, size*1.6) << QPoint(size/3, size*1.6) << QPoint(size, size) << QPoint(size, -size);
	QBrush brush;
	brush.setColor(QColor("DarkRed")); brush.setStyle(Qt::SolidPattern);
	robot = scene.addPolygon(poly2, QPen(QColor("DarkRed")), brush);
	robot->setPos(0, -1000);
	robot->setRotation(0);
	robot->setZValue(1);
	robot_nose = new QGraphicsEllipseItem(QRectF(-5,-5, 100, 100), robot);
	robot_nose->setBrush(QBrush(QColor("LightGreen")));
	robot_nose->setPos(-40,200);
	//robot_nose = scene.addEllipse(QRectF(-5,-5, 10, 10));
	//robot_nose->setParentItem(robot);
	const auto robotN = robot_nose->mapToScene(QPointF(0,0));
	qDebug() << robot_nose->pos() << robotN;

	// target
	target = scene.addRect(QRectF(-80, -80, 160, 160));
	target->setFlag(QGraphicsItem::ItemIsMovable);
	target->setPos(robotN);
	target->setBrush(QColor("LightBlue"));
    active = false;

	// bind first and last to robot and target
	auto ellipse = scene.addEllipse(QRectF(-50,-50, 100, 100), QPen(QColor("LightGreen")), QBrush(QColor("LightGreen")));
	ellipse->setFlag(QGraphicsItem::ItemIsMovable);
	points.push_back(ellipse);
	first = points.front();
	first->setPos(robotN);
	first->setBrush(QColor("MediumGreen"));

    //People
	humanA = new Human(QRectF(-400,-400,800,800), socialnavigationgaussian_proxy, &scene, QColor("LightBlue"), QPointF(2500, -2000));
	scene.addItem(humanA);
//	human_vector.push_back(humanA);
	humanB = new Human(QRectF(-400,-400,800,800), socialnavigationgaussian_proxy, &scene, QColor("LightGreen"), QPointF(2500, -2900));
	scene.addItem(humanB);
//	human_vector.push_back(humanB);
	
	//Axis   
	auto axisX = scene.addRect(QRectF(0, 0, 200, 20), QPen(Qt::red), QBrush(QColor("red")));
	boxes.push_back(axisX);
	auto axisZ = scene.addRect(QRectF(0, 0, 20, 200), QPen(Qt::blue), QBrush(QColor("blue")));
	boxes.push_back(axisZ);
	
	// Laser
	for( auto &&i : iter::range(-M_PI/2.f, M_PI/2.f, M_PI/LASER_ANGLE_STEPS) )
		laserData.emplace_back(LData{0.f, (float)i});
	
	//Grid
	grid.initialize( TDim{ TILE_SIZE, LEFT, BOTTOM, WIDTH, HEIGHT}, TCell{0, true, false, nullptr, 1.f} );
	// check is cell key.x, key.z is free by checking is there are boxes in it
	std::uint32_t id_cont=0;
	auto robot_back = robotN;
	for(auto &&[k, cell] : grid)
	{
		//robot->setPos(k.x, k.z);
		cell.g_item = scene.addEllipse(QRectF(-200, -200, 200, 200));
		cell.g_item->setPos(k.x, k.z);
		cell.g_item->setZValue(-1);		
		cell.g_item->setPen(QPen(QColor("OldLace")));
		cell.g_item->setBrush(QBrush(QColor("OldLace"),  Qt::Dense6Pattern));
		cell.id = id_cont++;
		cell.free = true;
		if(std::any_of(std::begin(boxes), std::end(boxes),[k, this](auto &box){ return box->contains(box->mapFromScene(QPointF(k.x,k.z)));}))
		{
		 		cell.free = false;		
		 		cell.g_item->setPen(QPen(QColor("WhiteSmoke")));
		 		cell.g_item->setBrush(QBrush(QColor("WhiteSmoke")));
		}
		// for(auto &&p: robot->polygon())
		// 	if(std::any_of(std::begin(boxes), std::end(boxes),[p, this](auto &box){ return box->contains(box->mapFromItem(robot, p));}))
		// 	{
		// 		cell.free = false;		
		// 		cell.g_item->setPen(QPen(QColor("WhiteSmoke")));
		// 		cell.g_item->setBrush(QBrush(QColor("WhiteSmoke")));
		// 		break;
		// 	}
	}
	// Set high cost to cell touching obstacles
	for(auto &cell : grid)
	{
		auto list_n = grid.neighboors(cell.first); //Key
		if( std::any_of(std::begin(list_n), std::end(list_n), [](const auto &n){ return n.second.free == false;}))
			cell.second.cost = 5.f;
	}
	//robot->setPos(robot_back);
	qDebug() << "Grid initialize ok";

	// compute
	timer.start(100);
	
	// clean path
	connect(&cleanTimer, &QTimer::timeout, this, &SpecificWorker::cleanPath);
	//cleanTimer.start(50);
	
	// controller
    connect(&controllerTimer, &QTimer::timeout, this, &SpecificWorker::controller);
    //controllerTimer.start(200);
	
	// Proxemics
	//	connect(humanA, &Human::personChangedSignal, this, &SpecificWorker::personChangedSlot);
	//	connect(humanB, &Human::personChangedSignal, this, &SpecificWorker::personChangedSlot);
}

//load world model from file
void SpecificWorker::initializeWorld()
{
    QString val;
    QFile file(QString::fromStdString(params["World"].value));
    if(not file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qDebug()<<"Error reading world file, check config params:"<<QString::fromStdString(params["World"].value);
        exit(-1);
    }
    val = file.readAll();
    file.close();
    QJsonDocument doc = QJsonDocument::fromJson(val.toUtf8());
    QJsonObject jObject = doc.object();
    QVariantMap mainMap = jObject.toVariantMap();
    //load tables
    QVariantMap tables = mainMap[QString("tables")].toMap();
    for (auto &t: tables)
    {
        QVariantList object = t.toList();
        auto box = scene.addRect(QRectF(-object[2].toFloat()/2, -object[3].toFloat()/2, object[2].toFloat(), object[3].toFloat()), QPen(QColor("SandyBrown")), QBrush(QColor("SandyBrown")));
        box->setPos(object[4].toFloat(), object[5].toFloat());
        box->setRotation(object[6].toFloat());
        boxes.push_back(box);
    }
    //load roundtables
    QVariantMap rtables = mainMap[QString("roundTables")].toMap();
    for (auto &t: rtables)
    {
        QVariantList object = t.toList();
        auto box = scene.addEllipse(QRectF(-object[2].toFloat()/2, -object[3].toFloat()/2, object[2].toFloat(), object[3].toFloat()), QPen(QColor("Khaki")), QBrush(QColor("Khaki")));
        box->setPos(object[4].toFloat(), object[5].toFloat());
        boxes.push_back(box);
    }
    //load walls
    QVariantMap walls = mainMap[QString("walls")].toMap();
    for (auto &t: walls)
    {
        QVariantList object = t.toList();
        auto box = scene.addRect(QRectF(-object[2].toFloat()/2, -object[3].toFloat()/2, object[2].toFloat(), object[3].toFloat()), QPen(QColor("Brown")), QBrush(QColor("Brown")));
        box->setPos(object[4].toFloat(), object[5].toFloat());
        //box->setRotation(object[6].toFloat()*180/M_PI2);
        boxes.push_back(box);
    }
    
    //load points
    QVariantMap points = mainMap[QString("points")].toMap();
    for (auto &t: points)
    {
         QVariantList object = t.toList();
         auto box = scene.addRect(QRectF(-object[2].toFloat()/2, -object[3].toFloat()/2, object[2].toFloat(), object[3].toFloat()), QPen(QColor("Brown")), QBrush(QColor("Brown")));
         box->setPos(object[4].toFloat(), object[5].toFloat());
  		//box->setRotation(object[6].toFloat()*180/M_PI2);
         boxes.push_back(box);
    }
    
}

// SLOTS
void SpecificWorker::compute()
{
	computeLaser(robot_nose, boxes);  // goes
	computeVisibility(points, laser_polygon);
	computeForces(points, laserData);
	cleanPath();
	controller();
	updateRobot();				// goes
	reloj.restart();
}

void SpecificWorker::cleanPath()
{
	//computeForces(points, laserData);
	addPoints();
	cleanPoints();
}

/////////////////////////////////////////////////////////////////////7
/////////
////////////////////////////////////////////////////////////////////

void SpecificWorker::createPathFromGraph(const std::list<QVec> &path)
{
	for(auto &&p: points)
		scene.removeItem(p);
	points.clear();
	for(auto &&p: path)
	{
		auto ellipse = scene.addEllipse(QRectF(-50,-50, 100, 100), QPen(QColor("LightGreen")), QBrush(QColor("LightGreen")));
		ellipse->setFlag(QGraphicsItem::ItemIsMovable);
		ellipse->setPos(p.x(), p.z());
		points.push_back(ellipse);
	}
	first = points.front();
	first->setPos(robot_nose->mapToScene(QPointF(0,100))); //beyond nodw
	first->setBrush(QColor("MediumGreen"));
	last = points.back();
	last->setPos(target->pos());
	target->setZValue(1);
}

void SpecificWorker::computeVisibility(const std::vector<QGraphicsEllipseItem*> &path, const QGraphicsPolygonItem *laser)
{
	const auto &poly = laser->polygon();
	for(auto &&p: path)
	{
		if( poly.containsPoint(p->pos(), Qt::OddEvenFill))
		{
			p->setData(0, true);
			p->setBrush(QColor("LightGreen"));
		}
		else 
		{
			p->setData(0, false);
			p->setBrush(QColor("DarkGreen"));
		}
	}
}

void SpecificWorker::computeForces(const std::vector<QGraphicsEllipseItem*> &path, const std::vector<LData> &lData)
{
	if(path.size() < 3) return;

	auto &lpath = path;

	// baselines needed to remove tangential component
	std::vector<QVector2D> base_lines(lpath.size(), QVector2D(0.f,0.f));
	int k=1;
	for(auto group : iter::sliding_window(lpath, 3))
	{
		if(group.size()<3) break;

		auto p1 = QVector2D(group[0]->pos());
		auto p3 = QVector2D(group[2]->pos());
		base_lines[k++] = (p1 - p3).normalized();
	}  
	
	// internal curvature forces
	std::vector<QVector2D> iforces(lpath.size(), QVector2D(0.f,0.f));
	k=1;
	for(auto group : iter::sliding_window(lpath, 3))
	{
		if(group.size()<3) break;

		auto p1 = QVector2D(group[0]->pos());
		auto p2 = QVector2D(group[1]->pos());
		auto p3 = QVector2D(group[2]->pos());

		//internal force on p2
		auto iforce = ((p1-p2)/(p1-p2).length() + (p3-p2)/(p3-p2).length());
		//QVector2D iforce(0,0);
		//if(group[1]->data(0).value<bool>() == true) // inside laser field
		//auto iforce = (p1-p2) + (p3-p2);
		iforces[k++] = iforce;
	} 
	
	// external forces
	// we need the minimun distance from each point to the obstacle(s) we compute the closest laser ray tip to each point
	std::vector<QVector2D> eforces;
	static std::vector<QGraphicsLineItem*> lforces;
	
	for(auto &&f: lforces)
		scene.removeItem(f);
	lforces.clear();
	for( auto &&p : lpath)
	{
		QVector2D force(0,0), f_force(0,0);
		if(p->data(0).value<bool>() == true) // inside laser field
		{
			std::vector<std::tuple<float, QVector2D>> distances;
			std::transform(std::begin(lData), std::end(lData), std::back_inserter(distances), [p, this](auto &l)
				{ 
					auto t = QPointF(l.dist*sin(l.angle), l.dist*cos(l.angle));
					QVector2D tip(robot->mapToScene(t));
					if(QVector2D(t).length() < MAX_LASER_DIST-1)
						return std::make_tuple((QVector2D(p->pos()) - tip).length(), QVector2D(p->pos()) - tip);	
					else
						return std::make_tuple(MAX_LASER_DIST, QVector2D(0,0));	
				});
			auto min = std::min_element(std::begin(distances), std::end(distances), [](auto &a, auto &b){return std::get<float>(a) < std::get<float>(b);});
			force = std::get<QVector2D>(*min);
			float mag = force.length();
			//linear inverse law  y = -1/4 x + 200
			if(mag > 800) mag = 800;
			mag  = -(200.f/800)*mag + 200.f;
			f_force = mag * force.normalized();	
		}
		
		lforces.push_back(scene.addLine(QLineF( p->pos(), p->pos()+f_force.toPointF())));
		eforces.push_back(f_force);
	}

	//Apply forces to current position
	const float KE = 0.1;
	const float KI = 1.5;	
	//const float KL = 0.06;	
	for(auto &&[point, iforce, eforce, base_line] : iter::zip(lpath, iforces, eforces, base_lines))
	{
		if( point->data(0).toBool() == false) //not visible
			continue;

		const auto &idelta = KI * iforce.toPointF();
		QPointF edelta{0,0}; 
		edelta = KE * eforce.toPointF();
		
		const auto force =  idelta + edelta;

		// Removing tangential component
		const auto eprod = QVector2D::dotProduct(QVector2D(force), base_line);
		if(eprod < 5)
		{
			const auto corrected_force = force - (QVector2D::dotProduct(QVector2D(force), base_line)*base_line).toPointF();
			point->setPos( point->pos() + corrected_force ); 
		}
		else
			point->setPos( point->pos() + force ); 
	}
	// reposition endpoints
	first->setPos(robot_nose->mapToScene(QPointF(0,0)));
	last->setPos(target->pos());
	second = points[1];
}

////// Add points to the path if needed
void SpecificWorker::addPoints()
{
	std::vector<std::tuple<int, QPointF>> points_to_insert;
	int k=1;
	for(auto &&group: iter::sliding_window(points, 2))
	{
		auto &p1 = group[0];
		auto &p2 = group[1];
		float dist = QVector2D(p1->pos()-p2->pos()).length();
		if (dist > ROAD_STEP_SEPARATION)  
		{
			float l = 0.9 * ROAD_STEP_SEPARATION / dist;   //Crucial que el punto se ponga mas cerca que la condición de entrada
			QLineF line(p1->pos(), p2->pos());
			points_to_insert.push_back(std::make_tuple(k, QPointF{line.pointAt(l)}));
		}
		k++;
	}
	int l=0;
	for(auto &&p : points_to_insert)
	{
		auto r = scene.addEllipse(QRectF(-50,-50,100,100), QPen(QColor("Green")), QBrush(QColor("Green")));
		r->setPos(std::get<QPointF>(p));
		points.insert(points.begin() + std::get<int>(p) + l++, r);
	}
	//qDebug() << points.size();
}

////// Remove points fromthe path if needed
void SpecificWorker::cleanPoints()
{
	std::vector<QGraphicsEllipseItem*> points_to_remove;
	for(const auto &group : iter::sliding_window(points, 2))
	{
		const auto &p1 = group[0];
		const auto &p2 = group[1];
		if(p2 == last)
			break;
		float dist = QVector2D(p1->pos()-p2->pos()).length();
		if (dist < 0.5 * ROAD_STEP_SEPARATION)  
			points_to_remove.push_back(p2);
	}
	for(auto p: points_to_remove)
	{
		scene.removeItem(p);
		points.erase(std::remove_if(points.begin(), points.end(), [p](auto &r){ return p==r;}), points.end());
	}
}

////// Render synthetic laser
void SpecificWorker::computeLaser(QGraphicsItem *r, const std::vector<QGraphicsItem*> &box)
{
	std::vector<QGraphicsItem*> boxes_temp = boxes;
	// for(auto &[id, polygon, item]: human_poly)
	// 	boxes_temp.push_back(item);
	for(auto &&item: human_poly)
		boxes_temp.push_back(item);

	for( auto &&l : laserData )
	{
		l.dist = MAX_LASER_DIST;
		QLineF line(r->mapToScene(QPointF(0,0)), r->mapToScene(QPointF(MAX_LASER_DIST*sin(l.angle), MAX_LASER_DIST*cos(l.angle))));
		for( auto t : iter::range(0.f, 1.f, LASER_DIST_STEP))
		{
			auto point = line.pointAt(t);
			if(std::any_of(std::begin(boxes_temp), std::end(boxes_temp),[point](auto &box){ return box->contains(box->mapFromScene(point));}))
			{
				l.dist = QVector2D(point-line.pointAt(0)).length();
				break;
			}
		}
	}
	if(laser_polygon != nullptr)
		scene.removeItem(laser_polygon);
	QPolygonF poly;
	for(auto &&l : laserData)
		poly << r->mapToScene(QPointF(l.dist*sin(l.angle), l.dist*cos(l.angle)));
	
	QColor color("Linen"); color.setAlpha(180);
	laser_polygon = scene.addPolygon(poly, QPen(color), QBrush(color));
	laser_polygon->setZValue(-1);
}

void SpecificWorker::controller()
{
	// Check for inminent collision to block and bounce backwards using laser field

    if (not active)
	{  return; }
	
	// Compute distance to target along path
	float dist_to_target = 0.f;
	for(auto &&g : iter::sliding_window(points, 2))
		dist_to_target += QVector2D(g[1]->pos() - g[0]->pos()).length();
	
	// Check for arrival to target
	if(dist_to_target < ROBOT_LENGTH)
	{
        std::cout << "TARGET ACHIEVED" << std::endl;
		advVelz = 0;
		rotVel = 0;
        active = false;
		return;
	}

	// Check for blocking
	auto num_free = std::count_if(std::begin(points), std::end(points), [](auto &&p){ return p->data(0) == true;});
	//qDebug() << "num free " << num_free;
	if(num_free < ROBOT_LENGTH / ROAD_STEP_SEPARATION)
	{
		qDebug() << __FUNCTION__ << "Blocked!";
		advVelz = 0;
		rotVel = 0;
		std::list<QVec> lpath = grid.computePath(Grid<TCell>::Key(robot->pos()), Grid<TCell>::Key(target->pos()));
		if(lpath.size() > 0) createPathFromGraph(lpath);
		else return;
	}

	// Proceed through path
	// Compute rotation speed. We use angle between robot's nose and line between first and sucessive points
	// as an estimation of curvature ahead
	std::vector<float> angles;
	auto lim = std::min(6, (int)points.size());
	QLineF nose(robot->pos(), robot->mapToScene(QPointF( 0, 50)));
	for(auto &&i: iter::range(1,lim))
		angles.push_back(rewrapAngleRestricted(qDegreesToRadians(nose.angleTo(QLineF(first->pos(),points[i]->pos())))));
	auto min_angle = std::min(angles.begin(), angles.end());	
	
	rotVel = 1.2 * *min_angle;
	if (fabs(rotVel) > ROBOT_MAX_ROTATION_SPEED)
		rotVel = rotVel/fabs(rotVel) * ROBOT_MAX_ROTATION_SPEED;

	// Compute advance speed
	std::min( advVelz = ROBOT_MAX_ADVANCE_SPEED * exponentialFunction(rotVel, 0.3, 0.5, 0) , dist_to_target);
	//std::cout <<  "In controller: active " << active << " adv: "<< advVelz << " rot: " << rotVel << std::endl;
	
}

///Periodic update of robot's state based on its adv and rot speeds.
void SpecificWorker::updateRobot()
{ 
	if (not active)
	{  return; }
	
	auto robot_current_pos = robot->pos();
	QVec incs = QVec::vec3(0, advVelz, -rotVel) * ((float)reloj.restart() / 1000.f);
	double alpha = robot->rotation() + qRadiansToDegrees(incs.z());
	double alpha_mat = qDegreesToRadians(robot->rotation() + incs.z());
	QMat m(3,3); 
	m(0,0) = cos(alpha_mat); m(0,1) = -sin(alpha_mat); m(0,2) = robot_current_pos.x();
	m(1,0) = sin(alpha_mat); m(1,1) = cos(alpha_mat);  m(1,2) = robot_current_pos.y();
	m(2,0) = 0; m(2,1) = 0;  m(2,2) = 1;
	auto new_pose = m * QVec::vec3(incs.x(), incs.y(), 1);
	robot->setRotation(alpha);
	robot->setPos(new_pose.x(), new_pose.y());
	//std::cout << "In update " << alpha << " " << new_pose.x() << " " << new_pose.y() << std::endl;
}

//////////////////////////////////////////////////////////////////777
////// Utilities
////////////////////////////////////////////////////////////////////
float SpecificWorker::rewrapAngleRestricted(const float angle)
{	
	if(angle > M_PI)
   		return angle - M_PI*2;
	else if(angle < -M_PI)
   		return angle + M_PI*2;
	else return angle;
}

// compute max de gauss(value) where gauss(x)=y  y min
float SpecificWorker::exponentialFunction(float value, float xValue, float yValue, float min)
{
	if( yValue <= 0) return 1.f;
	float landa = -fabs(xValue) / log(yValue);
	float res = exp(-fabs(value)/landa);
	return std::max(res, min);
}

///////////////////////////////////////////////////////////////////////////////////////7
///// Qt Events
////////////////////////////////////////////////////////////////////////////////////////

void SpecificWorker::mousePressEvent(QMouseEvent *event)
{
	if(event->button() == Qt::LeftButton)
	{
		auto p = view.mapToScene(event->x(), event->y());	
		qDebug() << __FUNCTION__ << "target " << p;
		std::list<QVec> path = grid.computePath(Grid<TCell>::Key(robot->pos()), Grid<TCell>::Key(p));
		if(path.size() > 0) 
		{
			createPathFromGraph(path);
			target->setPos(p);
            active = true;
		}
		// for(auto &&p: path)
		// 	p.print("p");
		// qDebug() << "-----------";
	}
	if(event->button() == Qt::RightButton)
	{
		// check the item is a person
		// assign mouse y coordinate to current person angle
		// move person angle with mouse y coordinate
		
	}
}

// zoom
void SpecificWorker::wheelEvent(QWheelEvent *event)
{
	const QGraphicsView::ViewportAnchor anchor = view.transformationAnchor();
	view.setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
	int angle = event->angleDelta().y();
	qreal factor;
	if (angle > 0) 
	{
		factor = 1.1;
		QRectF r = scene.sceneRect();
		this->scene.setSceneRect(r);
	}
	else
	{
		factor = 0.9;
		QRectF r = scene.sceneRect();
		this->scene.setSceneRect(r);
	}
	view.scale(factor, factor);
	view.setTransformationAnchor(anchor);

	QSettings settings("RoboComp", "ElasticPath");
	settings.beginGroup("QGraphicsView");
		settings.setValue("matrix", view.transform());
	settings.endGroup();
}

void SpecificWorker::resizeEvent(QResizeEvent *event)
{
	QSettings settings("RoboComp", "ElasticPath");
	settings.beginGroup("MainWindow");
		settings.setValue("size", event->size());
	settings.endGroup();
}

///////////////////////////////////////////////////////////////////////7
//////////  Proxemics slot
//////////////////////////////////////////////////////////////////////////

// Create a list of Humans or a Class so when one of the humans change
// all the list is evaluated for composite gaussians if proximity conditions are met

void SpecificWorker::personChangedSlot(Human *human)
{
	// Go through the list of humans to check for composite gaussians
	// Using the whole list, create the modified laser field
	// Modify the Grid
	RoboCompSocialNavigationGaussian::SNGPersonSeq persons;
	for (auto human: human_vector)
	{
		RoboCompSocialNavigationGaussian::SNGPerson p{ float(human->x())/1000.f, float(human->y())/1000.f, float(degreesToRadians(human->rotation())) + float(M_PI), 0.f, 0 };
		persons.push_back(p);
	}
	RoboCompSocialNavigationGaussian::SNGPolylineSeq personal_spaces;
	try
	{
		personal_spaces = socialnavigationgaussian_proxy->getPersonalSpace(persons, 0.9, false);
		if(personal_spaces.size()==0) return;
		if(personal_spaces[0].size()==0) return;
	}
	catch(...)
	{
		qDebug() << __FUNCTION__ << "Error reading personal space from SocialGaussian";
	}
	
	std::vector<QPolygonF> new_polys;
	for(auto &&poly: personal_spaces)
	{
		QPolygonF polygon;
		for(auto &&p: poly)
			polygon << QPointF(p.x*1000.f,p.z*1000.f);
		new_polys.push_back(polygon);
	}
	updateFreeSpaceMap(new_polys);
}

void SpecificWorker::updateFreeSpaceMap(const std::vector<QPolygonF> &new_polys)
{
	// First remove polygons from last iteration respecting cell occupied by furniture
	for(auto &&p: human_poly)
	{
		grid.markAreaInGridAs(p->polygon(), true);
		scene.removeItem(p);
	}
	// Then insert new_polys
	human_poly.clear();
	for(auto &&p: new_polys)
	{
		human_poly.push_back(scene.addPolygon(p, QColor("LightGreen") /*,QBrush(QColor("LightGreen"))*/));
		grid.markAreaInGridAs(p, false);
	}
}

float SpecificWorker::degreesToRadians(const float angle_)
{	
	float angle = angle_ * 2*M_PI / 360;
	if(angle > M_PI)
   		return angle - M_PI*2;
	else if(angle < -M_PI)
   		return angle + M_PI*2;
	else return angle;
}




//////////////////////////77

//void SpecificWorker::updateRobot()
//{
	//static QTime reloj = QTime::currentTime();
	// Read pose from robot
	/* RoboCompGenericBase::TBaseState sensorbState, robotState;
	try
    {
        genericbase_proxy->getBaseState(sensorbState);
		differentialrobot_proxy->getBaseState(robotState);
		advVelz = robotState.advVx;		
		rotVel = robotState.rotV;
		qDebug()<<"real speed" << advVelz << rotVel;
    }
    catch(...)
    {
        std::cout << "Error getting robot position from genericBase" << std::endl;
    }
	if (not active)
	{
		bState = sensorbState;
		qDebug()<< "time"<<reloj.restart()/ 1000.f;
	}
	else
	{
		QVec Pr = QVec::vec3(bState.x, bState.z, bState.alpha); //anterior position
		QVec Ps = QVec::vec3(sensorbState.x, sensorbState.z, sensorbState.alpha);
		
		// estimate position using speed sent to robot
		QVec incs = QVec::vec3(0, advVelz, -rotVel) * ((float)reloj.restart() / 1000.f);
		float alpha = bState.alpha + incs.z();
		
		QMat m(3,3); 
		m(0,0) = cos(alpha); m(0,1) = -sin(alpha); m(0,2) = bState.x;
		m(1,0) = sin(alpha); m(1,1) = cos(alpha);  m(1,2) = bState.z;
		m(2,0) = 0;          m(2,1) = 0;           m(2,2) = 1;
		
		auto Pe = m * QVec::vec3(incs.x(), incs.y(), 1);
		std::cout << "**********" <<std::endl;
		Pr.print("real");
		Pe.print("estimada");
		Ps.print("sensor");

		QPolygonF triangle;
		float dX = 200;
		float dY = 1000;
		QLineF line = QLineF(QPointF(Pe.x(),Pe.y()), QPointF(Pr.x(),Pr.y())).normalVector();
//qDebug()<<"line"<<line;
		line.setLength(dX);
//qDebug()<<"line left"<<line.x2()<<line.y2();
		triangle << QPointF(Pr.x(),Pr.y());
		triangle << QPointF(line.x2(),line.y2());
		line.setLength(-dX);
//qDebug()<<"line rightt"<<line.x2()<<line.y2();	
		triangle << QPointF(line.x2(), line.y2());
//qDebug()<<"Polygon"<<triangle;	
		scene.removeItem(localizationPolygon);
		localizationPolygon = scene.addPolygon(triangle, QColor("Yellow"), QBrush(QColor("Yellow")));
		localizationPolygon->setZValue(10);	 	
		
		scene.removeItem(spherePolygon);
		float size = 50 + lostMeasure*10;
		spherePolygon = scene.addEllipse(Pe.x()-size/2, Pe.y()-size/2, size, size, QColor("Orange"), QBrush(QColor("Orange")));
		
		//Check if sensor position is inside polygon
		if (spherePolygon->contains(QPointF(Ps.x(), Ps.y())))
		{
			std::cout << "sensor position is valid" << std::endl;
			spherePolygon->setBrush(QBrush(QColor("Blue")));
			bState = sensorbState;
			lostMeasure = 0;
		}
		else
		{
			std::cout << "sensor position is outside estimate limits" << std::endl;
			spherePolygon->setBrush(QBrush(QColor("Orange")));
			bState.x = Pe.x();
			bState.z = Pe.y();
			bState.alpha = alpha;
			lostMeasure += 1;
		}
	} */
	// update robot graphical robot position
	//robot->setPos(bState.x, bState.z);
	//robot->setRotation(qRadiansToDegrees(bState.alpha));
    
//}