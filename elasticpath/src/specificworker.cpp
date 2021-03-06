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
#include <cppitertools/filter.hpp>
#include <cppitertools/enumerate.hpp>
#include <cppitertools/slice.hpp>
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
		params = _params;
	}
	catch (const std::exception &e)
	{
		qFatal("Error reading config params");
	}
	return true;
}

void SpecificWorker::initialize(int period)
{
	std::cout << "Initialize worker" << std::endl;

	//Load World
	initializeWorld();

	resize(QDesktopWidget().availableGeometry(this).size() * 0.6);
	scene.setSceneRect(dimensions.HMIN, dimensions.VMIN, dimensions.WIDTH, dimensions.HEIGHT);
	graphicsView->setViewport(new QGLWidget);
	graphicsView->scale(1, -1);
	graphicsView->setScene(&scene);
	graphicsView->fitInView(scene.sceneRect(), Qt::KeepAspectRatio);

	// reward viewer
	chartView.setParent(frame);
	chartView.resize(frame->size());
	chartView.setChart(&chart);
	chart.addSeries(&curve);
	chart.createDefaultAxes();
	chartView.show();
	

	// Robot
	QPolygonF poly2;
	float size = ROBOT_LENGTH / 2.f;
	poly2 << QPoint(-size, -size)
		  << QPoint(-size, size)
		  << QPoint(-size / 3, size * 1.6)
		  << QPoint(size / 3, size * 1.6)
		  << QPoint(size, size)
		  << QPoint(size, -size);

	QBrush brush;
	brush.setColor(QColor("DarkRed"));
	brush.setStyle(Qt::SolidPattern);
	robot = scene.addPolygon(poly2, QPen(QColor("DarkRed")), brush);
	robot->setFlag(QGraphicsItem::ItemIsMovable);
	robot->setPos(0, -1000);
	robot->setRotation(0);
	robot->setZValue(1);
	robot_nose = new QGraphicsEllipseItem(QRectF(-5, -5, 10, 10), robot);
	robot_nose->setBrush(QBrush(QColor("LightGreen")));
	robot_nose->setPos(0, 360);
	const auto robotN = robot_nose->mapToScene(QPointF(0, 0));
	laser_pose = new QGraphicsEllipseItem(QRectF(-5, -5, 10, 10), robot);
	laser_pose->setPos(0, 190);

	// target
	target = scene.addRect(QRectF(-80, -80, 160, 160));
	target->setFlag(QGraphicsItem::ItemIsMovable);
	target->setPos(robotN);
	target->setBrush(QColor("LightBlue"));

	// bind first and last to robot and target
	auto ellipse = scene.addEllipse(QRectF(-50, -50, 100, 100), QPen(QColor("LightGreen")), QBrush(QColor("LightGreen")));
	ellipse->setFlag(QGraphicsItem::ItemIsMovable);
	points.push_back(ellipse);
	first = points.front();
	first->setPos(robotN);
	first->setBrush(QColor("MediumGreen"));

	//People
	humanA = new Human(QRectF(-400, -400, 800, 800), socialnavigationgaussian_proxy, &scene, QColor("LightBlue"), QPointF(2500, -2000));
	scene.addItem(humanA);
	//	human_vector.push_back(humanA);
	humanB = new Human(QRectF(-400, -400, 800, 800), socialnavigationgaussian_proxy, &scene, QColor("LightGreen"), QPointF(2500, -2900));
	scene.addItem(humanB);
	//	human_vector.push_back(humanB);

	//Axis   PUT IN JSON
	// auto axisX = scene.addRect(QRectF(0, 0, 200, 20), QPen(Qt::red), QBrush(QColor("red")));
	// boxes.push_back(axisX);
	// auto axisZ = scene.addRect(QRectF(0, 0, 20, 200), QPen(Qt::blue), QBrush(QColor("blue")));
	// boxes.push_back(axisZ);

	// Laser
	for (auto &&i : iter::range(-3 * M_PI / 4, 3 * M_PI / 4, (3 * M_PI / 2.f) / LASER_ANGLE_STEPS))
		laserData.emplace_back(LData{0.f, (float)i});

	
	
	
	//Grid
	grid.initialize(dimensions, TCell{0, true, false, nullptr, 1.f});

	// Obstacles: check is cell key.x, key.z is free by checking is there are boxes in it
	std::uint32_t id_cont = 0;
	for (auto &[k, cell] : grid)
	{
		cell.g_item = scene.addEllipse(QRectF(-100, -100, 200, 200));
		cell.g_item->setPos(k.x, k.z);
		cell.g_item->setZValue(-1);
		cell.g_item->setPen(QPen(QColor("OldLace")));
		cell.g_item->setBrush(QBrush(QColor("OldLace"), Qt::Dense6Pattern));
		cell.id = id_cont++;
		cell.free = true; 
		if (std::any_of(std::begin(boxes), std::end(boxes), [k, this](auto &box) { return box->contains(box->mapFromScene(QPointF(k.x, k.z))); }))
		{
			cell.free = false;
			cell.g_item->setPen(QPen(QColor("Red")));
			cell.g_item->setBrush(QBrush(QColor("Red")));
		}
	}
	// Set very high cost to obstacles touching cells
	for (auto &[k, cell] : grid)
	{
		auto list_n = grid.neighboors(k, true); //Key
		if (std::any_of(std::begin(list_n), std::end(list_n), [cell](const auto &n) 
			{ return cell.cost != 200 and n.second.free == false; }))
		{
			cell.cost = 200;
			cell.g_item->setPen(QPen(QColor("Cyan")));
			cell.g_item->setBrush(QBrush(QColor("Cyan")));
		}
	}
	// Set high cost to high cost touching cells
	for (auto &[k, cell] : grid)
	{
		auto list_n = grid.neighboors(k, true); //Key
		if (std::any_of(std::begin(list_n), std::end(list_n), [cell](const auto &n) 
			{ return cell.cost != 200 and n.second.cost == 200; }))
		{
			cell.cost = 50;
			cell.g_item->setPen(QPen(QColor("Yellow")));
			cell.g_item->setBrush(QBrush(QColor("Yellow")));
		}
	}

	//Sliders
	this->verticalSlider->setValue(KI);
	this->verticalSlider_2->setValue(KE * 50);
	this->verticalSlider_3->setValue(KB);
	connect(this->verticalSlider, &QSlider::valueChanged, this, [this](int v) { KI = v; });
	connect(this->verticalSlider_2, &QSlider::valueChanged, this, [this](int v) { KE = v / 50.f; });
	connect(this->verticalSlider_3, &QSlider::valueChanged, this, [this](int v) { KB = v; });
	
	qDebug() << "Grid initialize ok";

	// compute
	timer.start(100);
	reloj.restart();

	// clean path
	connect(&cleanTimer, &QTimer::timeout, this, &SpecificWorker::cleanPath);
	//cleanTimer.start(50);

	// controller
	connect(&controllerTimer, &QTimer::timeout, this, &SpecificWorker::controller);
	//controllerTimer.start(200);

	// Proxemics
	connect(humanA, &Human::personChangedSignal, this, &SpecificWorker::personChangedSlot);
	connect(humanB, &Human::personChangedSignal, this, &SpecificWorker::personChangedSlot);

	//showMaximized();

}

//load world model from file
void SpecificWorker::initializeWorld()
{
	QString val;
	QFile file(QString::fromStdString(params["World"].value));
	if (not file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		qDebug() << "Error reading world file, check config params:" << QString::fromStdString(params["World"].value);
		exit(-1);
	}
	val = file.readAll();
	file.close();
	QJsonDocument doc = QJsonDocument::fromJson(val.toUtf8());
	QJsonObject jObject = doc.object();
	QVariantMap mainMap = jObject.toVariantMap();
	//load dimensions
	QVariantMap dim = mainMap[QString("dimensions")].toMap();
	dimensions = TDim{dim["TILESIZE"].toInt(), dim["LEFT"].toFloat(), dim["BOTTOM"].toFloat(), dim["WIDTH"].toFloat(), dim["HEIGHT"].toFloat()};

	//load tables
	QVariantMap tables = mainMap[QString("tables")].toMap();
	for (auto &t : tables)
	{
		QVariantList object = t.toList();
		auto box = scene.addRect(QRectF(-object[2].toFloat() / 2, -object[3].toFloat() / 2, object[2].toFloat(), object[3].toFloat()), QPen(QColor("SandyBrown")), QBrush(QColor("SandyBrown")));
		box->setPos(object[4].toFloat(), object[5].toFloat());
		box->setRotation(object[6].toFloat());
		boxes.push_back(box);
	}
	//load roundtables
	QVariantMap rtables = mainMap[QString("roundTables")].toMap();
	for (auto &t : rtables)
	{
		QVariantList object = t.toList();
		auto box = scene.addEllipse(QRectF(-object[2].toFloat() / 2, -object[3].toFloat() / 2, object[2].toFloat(), object[3].toFloat()), QPen(QColor("Khaki")), QBrush(QColor("Khaki")));
		box->setPos(object[4].toFloat(), object[5].toFloat());
		boxes.push_back(box);
	}
	//load walls
	QVariantMap walls = mainMap[QString("walls")].toMap();
	for (auto &t : walls)
	{
		QVariantList object = t.toList();
		auto box = scene.addRect(QRectF(-object[2].toFloat() / 2, -object[3].toFloat() / 2, object[2].toFloat(), object[3].toFloat()), QPen(QColor("Brown")), QBrush(QColor("Brown")));
		box->setPos(object[4].toFloat(), object[5].toFloat());
		//box->setRotation(object[6].toFloat()*180/M_PI2);
		boxes.push_back(box);
	}

	//load points
	QVariantMap points = mainMap[QString("points")].toMap();
	for (auto &t : points)
	{
		QVariantList object = t.toList();
		auto box = scene.addRect(QRectF(-object[2].toFloat() / 2, -object[3].toFloat() / 2, object[2].toFloat(), object[3].toFloat()), QPen(QColor("Brown")), QBrush(QColor("Brown")));
		box->setPos(object[4].toFloat(), object[5].toFloat());
		//box->setRotation(object[6].toFloat()*180/M_PI2);
		boxes.push_back(box);
	}
	//load boxes
	QVariantMap cajas = mainMap[QString("boxes")].toMap();
	for (auto &t : cajas)
	{
		QVariantList object = t.toList();
		auto box = scene.addRect(QRectF(-object[2].toFloat() / 2, -object[3].toFloat() / 2, object[2].toFloat(), object[3].toFloat()), QPen(QColor("Brown")), QBrush(QColor("Orange")));
		box->setPos(object[4].toFloat(), object[5].toFloat());
		//box->setRotation(object[6].toFloat()*180/M_PI2);
		box->setFlag(QGraphicsItem::ItemIsMovable);
		boxes.push_back(box);
	}
}

///////////////////////////////////////////////////

void SpecificWorker::compute()
{
	 static std::vector<std::tuple<QPointF, float>> epochs;
	 auto [end, no_path, pos, bumper] = epoch();
	 if(end or no_path)
	{
		if(pushButtonAutomatic->isChecked())
		{
			//evaluatePath(epochs);
			getNextTarget();
			epochs.clear();
		}
	}
	else
		epochs.push_back(std::make_tuple(pos, bumper));
	reloj.restart();
}

/////////////////////////////////////////////////////////////////////////////////////////////////

void SpecificWorker::getNextTarget()
{
	//random generator
	//static std::random_device rd;
    //static std::mt19937 mt(rd());
    //static std::uniform_real_distribution<double> dist(1.0, 10.0);
	static bool flip = true;

	if( flip )
		current_target.p = QPointF(6206,3445);
		//current_target.p = QPointF(2141, -1212);
	else
		current_target.p = QPointF(0, -1000);
	current_target.active.store(true);
	this->current_target.blocked.store(true);
	flip = !flip;
}

void SpecificWorker::evaluatePath(const std::vector<std::tuple<QPointF, float>> &epochs)
{
	static std::vector<int> lens;
	static float accumulated_path = 0;
	static QPointF last_position = robot->pos();
	static int cont =0;
	
	//curve.setUseOpenGL(true);

	accumulated_path += QVector2D(robot->pos() - last_position).length();

	qDebug() << __FUNCTION__ << epochs.size();
	//lens.push_back(epochs.size());
	lens.push_back(accumulated_path);

	//chart.removeAllSeries();
	
	///for(auto &&[i, e]: iter::enumerate(lens))
	curve.append(cont++, accumulated_path);
	
	chart.show();
}

std::tuple<bool, bool, QPointF, float> SpecificWorker::epoch()
{
	if (this->current_target.active.load() == false)
		return std::make_tuple(true, false, QPointF(), 0.f );

	if (this->current_target.blocked.load() == true)
	{
		if (findNewPath() == false)
		{
			qDebug() << __FUNCTION__ << "Blocked: No path found in Compute";
			return std::make_tuple(false, true, QPointF(), 0.f );
		}
		this->current_target.blocked.store(false);
		reloj.restart();
	}

	computeLaser(laser_pose, boxes);
	computeVisibility(points, laser_polygon);
	cleanPath(); // might go in a faster timer
	controller();
	updateRobot();
	checkProgress();
	reloj.restart();
	return std::make_tuple(false, false, robot->pos(), bumperVel.length() );
}

// Check is the robot is stuck 
void SpecificWorker::checkProgress()
{
	static QTime reloj = QTime::currentTime();
	static float accumulated_path = 0;
	static QPointF last_position = robot->pos();

	accumulated_path += QVector2D(robot->pos() - last_position).length();
	qDebug() << accumulated_path;
	last_position = robot->pos();
}

void SpecificWorker::cleanPath()
{
	computeForces(points, laserData);
	cleanPoints();
	addPoints();
}

//////////////////////////////////////////////////////////////////////
/// Creates a new elasticband from the solution path obtained by A*
/////////////////////////////////////////////////////////////////////

void SpecificWorker::createPathFromGraph(const std::list<QVec> &path)
{
	if (path.empty())
		return;
	for (auto &&p : this->points)
		scene.removeItem(p);
	this->points.clear();
	for (const auto &p : path)
	{
		QRectF contorno(p.toQPointF(), QSizeF(BALL_SIZE, BALL_SIZE));
		if (robot->polygon().intersected(robot->mapFromScene(contorno)).empty())
		{
			auto ellipse = scene.addEllipse(QRectF(-BALL_MIN, -BALL_MIN, BALL_SIZE, BALL_SIZE), QPen(QColor("Black"), 10), QBrush(QColor("LightGreen")));
			ellipse->setFlag(QGraphicsItem::ItemIsMovable);
			ellipse->setPos(p.x(), p.z());
			points.push_back(ellipse);
		}
	}

	if (points.empty())
		return;
	first = points.front();
	first->setPos(robot_nose->mapToScene(QPointF(0, 0)));
	first->setBrush(QColor("MediumGreen"));
	////////////////////////// add real target
	last = points.back();
	last->setPos(target->pos());
	target->setZValue(1);

	//remove intial points in the path that are too close to the robot excluding robot and target
	std::vector<QGraphicsEllipseItem *> points_to_remove;
	for (auto &&p : iter::slice(points, 1, (int)points.size() - 1, 1))
	{
		if (robot->polygon().intersected(robot->mapFromScene(p->mapToScene(p->boundingRect()))).empty() == false)
			points_to_remove.push_back(p);
	}
	for (auto p : points_to_remove)
	{
		scene.removeItem(p);
		points.erase(std::remove_if(std::begin(points), std::end(points), [p](auto &r) { return p == r; }), std::end(points));
	}

	//Draw edges
	for (auto &&pair : iter::sliding_window(points, 2))
	{
		auto line = new QGraphicsLineItem(QLineF(QPointF(0, 0), pair[0]->mapFromScene(pair[1]->pos())), pair[0]);
		line->setPen(QPen(QColor("Red"), 20));
	}
	// for(auto &&p: points)
	// 	std::cout << "(" << p->x() << " " << p->y() << ")" << std::endl;
	// std::cout << std::endl;
}

////////////////////////////////////////////////
/////// Compute path nodes inside laser
////////////////////////////////////////////////

void SpecificWorker::computeVisibility(const std::vector<QGraphicsEllipseItem *> &path, const QGraphicsPolygonItem *laser)
{
	const auto &poly = laser->polygon();
	for (auto &&p : path)
	{
		if (poly.containsPoint(p->pos(), Qt::OddEvenFill) or robot->polygon().containsPoint(robot->mapFromScene(p->pos()), Qt::OddEvenFill))
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
	// for(auto &&p: path)
	//  	std::cout << "(" << p->pos().x() << " " << p->pos().y() << " " << p->data(0).toBool() << ")";
	// std::cout << std::endl;
}

////////////////////////////////////////////////
/////// Compute forces using Jacobian of distance from the robot to obstacle along a ray wrt to x,y position of the obstacle
////////////////////////////////////////////////


////////////////////////////////////////////////
/////// Compute forces using minimun distance computation
////////////////////////////////////////////////

void SpecificWorker::computeForces(const std::vector<QGraphicsEllipseItem *> &path, const std::vector<LData> &lData)
{
	if (path.size() < 3)
		return;

	//convert laser polar coordinates to cartesian
	std::vector<QPointF> laser_cart;
	QPolygonF laser_poly;
	for (const auto &l : lData)
	{
		QPointF p(robot->mapToScene(l.dist * sin(l.angle), l.dist * cos(l.angle)));
		laser_cart.push_back(p);
		laser_poly << p;
	}

	// Go through points using a sliding windows of 3
	for (auto group : iter::sliding_window(path, 3))
	{
		if (group.size() < 3)
			break; // break if too short

		auto p1 = QVector2D(group[0]->pos());
		auto p2 = QVector2D(group[1]->pos());
		auto p3 = QVector2D(group[2]->pos());
		auto p = group[1];

		if (isVisible(p) == false) // if not visible (computed before) continue
			continue;

		// INTERNAL curvature forces on p2
		QVector2D iforce = ((p1 - p2) / (p1 - p2).length() + (p3 - p2) / (p3 - p2).length());

		// EXTERNAL forces. We need the minimun distance from each point to the obstacle(s). we compute the shortest laser ray to each point in the path
		// compute minimun distances to each point within the laser field

		std::vector<std::tuple<float, QVector2D>> distances;
		// Apply to all laser points a functor to compute the distances to point p2
		std::transform(std::begin(laser_cart), std::end(laser_cart), std::back_inserter(distances), [p, this](QPointF &t) {
			// compute distante from laser tip to point minus RLENGTH/2 or 0 and keep it positive
			float dist = (QVector2D(p->pos()) - QVector2D(t)).length() - (ROBOT_LENGTH / 2);
			if (dist <= 0)
				dist = 0.01;
			return std::make_tuple(dist, QVector2D(p->pos()) - QVector2D(t));
		});

		// compute min distance
		auto min = std::min_element(std::begin(distances), std::end(distances), [](auto &a, auto &b) { return std::get<float>(a) < std::get<float>(b); });
		float min_dist = std::get<float>(*min);

		QVector2D force = std::get<QVector2D>(*min);
		// rescale min_dist so 1 is ROBOT_LENGTH
		float magnitude = (1.f / ROBOT_LENGTH) * min_dist;
		// compute inverse square law
		magnitude = 10.f / (magnitude * magnitude);
		//if(magnitude > 25) magnitude = 25.;
		QVector2D f_force = magnitude * force.normalized();
		//qDebug() << magnitude << f_force;

		// Remove tangential component of repulsion force by projecting on line tangent to path (base_line)
		QVector2D base_line = (p1 - p3).normalized();
		const QVector2D itangential = QVector2D::dotProduct(f_force, base_line) * base_line;
		f_force = f_force - itangential;

		// update node pos
		auto total = (KI * iforce) + (KE * f_force);

		// limiters CHECK!!!!!!!!!!!!!!!!!!!!!!
		if (total.length() > 30)
			total = 8 * total.normalized();
		if (total.length() < -30)
			total = -8 * total.normalized();

		// move node only if they do not exit the laser polygon and do not get inside objects or underneath the robot.
		QGraphicsEllipseItem *temp_p = new QGraphicsEllipseItem(p);
		temp_p->setPos(p->pos() + total.toPointF());
		if( laser_poly.intersected(temp_p->rect()).empty() 
			and
			//std::all_of(std::begin(boxes), std::end(boxes), [temp_p](const auto &box) { return box->rect().intersected(temp_p->rect()).empty();})
			std::none_of(std::begin(boxes), std::end(boxes), [p,total](const auto &box) { return box->contains(box->mapFromScene(p->pos() + total.toPointF()));})
			and 
			robot->polygon().intersected(temp_p->rect()).empty()
		  )
		  p->setPos(p->pos() + total.toPointF());

		// if (laser_poly.containsPoint(p->pos() + total.toPointF(), Qt::OddEvenFill)
		// 	and not
		// 	std::any_of(std::begin(boxes), std::end(boxes), [p,total](const auto &box) { return box->contains(box->mapFromScene(p->pos() + total.toPointF()));})
		// 	and not
		// 	robot->polygon().contains(robot->mapFromScene(p->pos() + total.toPointF()))
		// 	)
		// 	p->setPos(p->pos() + total.toPointF());

		// remove old arrows and edges
		for (const auto &pp : path)
			for (auto &&child : pp->childItems())
				scene.removeItem(child);

		// create new ones
		auto arrow = new QGraphicsLineItem(QLineF(QPointF(0, 0), p->mapFromScene(1.1 * (p->pos() + f_force.toPointF()))), p);
		auto point = new QGraphicsEllipseItem(0, 0, 40, 40, arrow);
		point->setBrush(QBrush(QColor("DarkGreen")));
		arrow->setPen(QPen(QBrush(QColor("DarkGreen")), 10));
		point->setPos(arrow->line().p2());
	}
	first->setPos(robot_nose->mapToScene(QPointF(0, 0)));
	last->setPos(target->pos());

	//Draw edges
	for (auto &&pair : iter::sliding_window(path, 2))
	{
		for (auto &&child : pair[0]->childItems())
		{
			auto line = qgraphicsitem_cast<QGraphicsLineItem *>(child);
			if (line)
				scene.removeItem(line);
		}
		auto line = new QGraphicsLineItem(QLineF(QPointF(0, 0), pair[0]->mapFromScene(pair[1]->pos())), pair[0]);
		line->setPen(QPen(QColor("Red"), 20));
	}
}

////////////////////////////////////////////////
////// Add points to the path if needed
////////////////////////////////////////////////

void SpecificWorker::addPoints()
{
	std::vector<std::tuple<int, QPointF>> points_to_insert;
	for (auto &&[k, group] : iter::enumerate(iter::sliding_window(points, 2)))
	{
		auto &p1 = group[0];
		auto &p2 = group[1];

		if (isVisible(p1) == false or isVisible(p2) == false) //not visible
			continue;

		float dist = QVector2D(p1->pos() - p2->pos()).length();
		if (dist > ROAD_STEP_SEPARATION)
		{
			float l = 0.9 * ROAD_STEP_SEPARATION / dist; //Crucial que el punto se ponga mas cerca que la condición de entrada
			QLineF line(p1->pos(), p2->pos());
			points_to_insert.push_back(std::make_tuple(k + 1, QPointF{line.pointAt(l)}));
		}
		//qDebug() << __FUNCTION__ << k;
	}
	for (const auto &[l, p] : iter::enumerate(points_to_insert))
	{
		auto r = scene.addEllipse(QRectF(-BALL_MIN, -BALL_MIN, BALL_SIZE, BALL_SIZE), QPen(QColor("Black"), 10), QBrush(QColor("LightGreen")));
		r->setPos(std::get<QPointF>(p));
		points.insert(points.begin() + std::get<int>(p) + l, r);
		// create edge between new point and second old point
		auto last = *(points.begin() + std::get<int>(p) + l + 1);
		auto line = new QGraphicsLineItem(QLineF(QPointF(0, 0), r->mapFromScene(last->pos())), r);
		line->setPen(QPen(QColor("Red"), 20));
		// resize edge from first old point to new point
		auto first = *(points.begin() + std::get<int>(p) + l - 1);
		line = qgraphicsitem_cast<QGraphicsLineItem *>(first);
		if (line)
		{
			line->line().setP2(first->mapFromScene(r->pos()));
			line->setPen(QPen(QColor("Red"), 20));
		}
	}
	//qDebug() << __FUNCTION__ << "points inserted " << points_to_insert.size();
}

////////////////////////////////////////////////7
////// Remove points fromthe path if needed
////////////////////////////////////////////////

void SpecificWorker::cleanPoints()
{
	std::vector<QGraphicsEllipseItem *> points_to_remove;
	for (const auto &group : iter::sliding_window(points, 2))
	{
		const auto &p1 = group[0];
		const auto &p2 = group[1];

		if ((p1->data(0).toBool() == false) or (p2->data(0).toBool() == false)) //not visible
			continue;
		if (p2 == last)
			break;
		// check if p1 was marked to erase in the previous iteration
		if (std::find(std::begin(points_to_remove), std::end(points_to_remove), p1) != std::end(points_to_remove))
			continue;

		float dist = QVector2D(p1->pos() - p2->pos()).length();
		if (dist < 0.5 * ROAD_STEP_SEPARATION)
			points_to_remove.push_back(p2);
	}
	for (auto &&p : points_to_remove)
	{
		points.erase(std::remove_if(points.begin(), points.end(), [p](auto &r) { return p == r; }), points.end());
		for (auto &&child : p->childItems())
			scene.removeItem(child);
		scene.removeItem(p);
	}
	//Repaint edges
	for (auto &&p : iter::sliding_window(points, 2))
		for (auto &&child : p[0]->childItems())
		{
			auto line = qgraphicsitem_cast<QGraphicsLineItem *>(child);
			if (line)
				line->setLine(QLineF(QPointF(0, 0), p[0]->mapFromScene(p[1]->pos())));
		}
}

////////////////////////////////////////////////7
///// Low level control of the robot
////////////////////////////////////////////////
void SpecificWorker::controller()
{
	// Compute distance to target along path
	// float dist_to_target = 0.f;
	// for(auto &&g : iter::sliding_window(points, 2))
	// 	dist_to_target += QVector2D(g[1]->pos() - g[0]->pos()).length();

	// Compute euclidean distance to target
	float euc_dist_to_target = QVector2D(target->pos() - robot->pos()).length();
	//qDebug() << __FUNCTION__  << points.size() << euc_dist_to_target;
	if (points.size() < 3 and euc_dist_to_target < FINAL_DISTANCE_TO_TARGET)
	{
		std::cout << "TARGET ACHIEVED" << std::endl;
		advVelz = 0;
		rotVel = 0;
		this->current_target.active.store(false);
		// clean points list		
		for (auto &&p : this->points)
			scene.removeItem(p);
		this->points.clear();
		return;
	}

	// Check for blocking and call replanning if needed
	auto num_free = std::count_if(std::begin(points), std::end(points), [](auto &&p) { return p->data(0) == true; });
	//if(num_free < ROBOT_LENGTH / ROAD_STEP_SEPARATION)
	if (num_free < 2)
	{
		qDebug() << __FUNCTION__ << "Blocked!  New path requested";
		advVelz = 0;
		rotVel = 0;
		this->current_target.blocked.store(true);
	}

	// Proceed through path
	// Compute rotation speed. We use angle between robot's nose and line between first and sucessive points
	// as an estimation of curvature ahead
	std::vector<float> angles;
	auto lim = std::min(6, (int)points.size());
	QLineF nose(robot->pos(), robot->mapToScene(QPointF(0, 50)));
	for (auto &&i : iter::range(1, lim))
		angles.push_back(rewrapAngleRestricted(qDegreesToRadians(nose.angleTo(QLineF(first->pos(), points[i]->pos())))));
	auto min_angle = std::min(angles.begin(), angles.end());

	if (min_angle != angles.end())
	{
		rotVel = 1.2 * *min_angle;
		if (fabs(rotVel) > ROBOT_MAX_ROTATION_SPEED)
			rotVel = rotVel / fabs(rotVel) * ROBOT_MAX_ROTATION_SPEED;
	}
	else
	{
		rotVel = 0;
		qDebug() << __FUNCTION__ << "rotvel = 0";
	}

	// Compute advance speed
	std::min(advVelz = ROBOT_MAX_ADVANCE_SPEED * exponentialFunction(rotVel, 0.3, 0.4, 0), euc_dist_to_target);
	//std::cout <<  "In controller: active " << active << " adv: "<< advVelz << " rot: " << rotVel << std::endl;

	// Compute bumper-away speed
	QVector2D total{0, 0};
	for (const auto &l : laserData)
	{
		float limit = (fabs(ROBOT_LENGTH / 2.f * sin(l.angle)) + fabs(ROBOT_LENGTH / 2.f * cos(l.angle))) + 200;
		float diff = limit - l.dist;
		if (diff >= 0)
			total = total + QVector2D(-diff * sin(l.angle), -diff * cos(l.angle));
	}
	
	bumperVel = total / KB;  // Parameter set in slidebar
}

bool SpecificWorker::findNewPath()
{
	// extract target from current_path
	this->current_target.lock();
		auto p = this->current_target.p;
	this->current_target.unlock();
	qDebug() << __FUNCTION__ << "robot" << robot->pos() << "target " << p;

	//mark spece under the robot as occupied
	grid.markAreaInGridAs(robot->mapToScene(robot->polygon()), false);
	std::list<QVec> path = grid.computePath(Grid<TCell>::Key(robot_nose->mapToScene(QPointF(0, 0))), Grid<TCell>::Key(p));
	if (path.size() > 0)
	{
		target->setPos(p);
		createPathFromGraph(path);
		//this->current_target.active.store(true);
		//this->current_target.blocked.store(false);
		grid.markAreaInGridAs(robot->mapToScene(robot->polygon()), true);
		return true;
	}
	else
	{
		//this->current_target.active.store(false);
		//this->current_target.blocked.store(true);
		qDebug() << __FUNCTION__ << "Path not found";
		grid.markAreaInGridAs(robot->mapToScene(robot->polygon()), true);
		return false;
	}
}

///////////////////////////////////////////////////////////////
////////   Methods to simulate physical robot
//////////////////////////////////////////////////////////////

///Periodic update of robot's state based on its adv and rot speeds.
void SpecificWorker::updateRobot()
{
	auto robot_current_pos = robot->pos();
	QVec incs = QVec::vec3(0, advVelz, -rotVel) * ((float)reloj.restart() / 1000.f);
	double alpha = robot->rotation() + qRadiansToDegrees(incs.z());
	double alpha_mat = qDegreesToRadians(robot->rotation() + incs.z());
	QMat m(3, 3);
	m(0, 0) = cos(alpha_mat);
	m(0, 1) = -sin(alpha_mat);
	m(0, 2) = robot_current_pos.x();
	m(1, 0) = sin(alpha_mat);
	m(1, 1) = cos(alpha_mat);
	m(1, 2) = robot_current_pos.y();
	m(2, 0) = 0;
	m(2, 1) = 0;
	m(2, 2) = 1;
	auto new_pose = m * QVec::vec3(incs.x(), incs.y(), 1);
	robot->setRotation(alpha);
	robot->setPos(new_pose.x(), new_pose.y());

	// add bumper derived force
	QPointF resX = robot->mapToScene(bumperVel.toPointF());
	robot->setPos(resX.x(), resX.y());
}

////// Render synthetic laser
void SpecificWorker::computeLaser(QGraphicsItem *r, const std::vector<QGraphicsItem *> &box)
{
	std::vector<QGraphicsItem *> boxes_temp = boxes;
	// for(auto &[id, polygon, item]: human_poly)
	// 	boxes_temp.push_back(item);
	for (auto &&item : human_poly)
		boxes_temp.push_back(item);

	for (auto &&l : laserData)
	{
		l.dist = MAX_LASER_DIST;
		QLineF line(r->mapToScene(QPointF(0, 0)), r->mapToScene(QPointF(MAX_LASER_DIST * sin(l.angle), MAX_LASER_DIST * cos(l.angle))));
		float step = 100.f / line.length();
		for (auto t : iter::range(0.f, 1.f, LASER_DIST_STEP))
		{
			auto point = line.pointAt(t);
			if (std::any_of(std::begin(boxes_temp), std::end(boxes_temp), [point](auto &box) { return box->contains(box->mapFromScene(point)); }))
			{
				l.dist = QVector2D(point - line.pointAt(0)).length() - (step * 2);
				break;
			}
		}
	}
	if (laser_polygon != nullptr)
		scene.removeItem(laser_polygon);
	QPolygonF poly;
	for (auto &&l : laserData)
		poly << r->mapToScene(QPointF(l.dist * sin(l.angle), l.dist * cos(l.angle)));

	QColor color("Pink");
	color.setAlpha(150);
	laser_polygon = scene.addPolygon(poly, QPen(color), QBrush(color));
	laser_polygon->setZValue(-1);
}

//////////////////////////////////////////////////////////////////777
////// Utilities
////////////////////////////////////////////////////////////////////
bool SpecificWorker::isVisible(const QGraphicsEllipseItem *p)
{
	return p->data(0).toBool();
}

float SpecificWorker::rewrapAngleRestricted(const float angle)
{
	if (angle > M_PI)
		return angle - M_PI * 2;
	else if (angle < -M_PI)
		return angle + M_PI * 2;
	else
		return angle;
}

// compute max de gauss(value) where gauss(x)=y  y min
float SpecificWorker::exponentialFunction(float value, float xValue, float yValue, float min)
{
	if (yValue <= 0)
		return 1.f;
	float landa = -fabs(xValue) / log(yValue);
	float res = exp(-fabs(value) / landa);
	return std::max(res, min);
}

///////////////////////////////////////////////////////////////////////////////////////7
///// Qt Events
////////////////////////////////////////////////////////////////////////////////////////

void SpecificWorker::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton)
	{
		auto p = graphicsView->mapToScene(event->x(), event->y());
		qDebug() << __FUNCTION__ << p;
		this->current_target.lock();
		current_target.active.store(true);
		current_target.blocked.store(true);
		current_target.p = p;
		this->current_target.unlock();
	}
	if (event->button() == Qt::RightButton)
	{
		// check the item is a person
		// assign mouse y coordinate to current person angle
		// move person angle with mouse y coordinate
	}
}

// zoom
void SpecificWorker::wheelEvent(QWheelEvent *event)
{
	const QGraphicsView::ViewportAnchor anchor = graphicsView->transformationAnchor();
	graphicsView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
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
	graphicsView->scale(factor, factor);
	graphicsView->setTransformationAnchor(anchor);

	QSettings settings("RoboComp", "ElasticPath");
	settings.beginGroup("QGraphicsView");
	settings.setValue("matrix", graphicsView->transform());
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
	qDebug() << "personChangedSlot";
	RoboCompSocialNavigationGaussian::SNGPersonSeq persons;
	for (auto human : human_vector)
	{
		RoboCompSocialNavigationGaussian::SNGPerson p{float(human->x()) / 1000.f, float(human->y()) / 1000.f, float(degreesToRadians(human->rotation())) + float(M_PI), 0.f, 0};
		persons.push_back(p);
	}
	RoboCompSocialNavigationGaussian::SNGPolylineSeq personal_spaces;
	try
	{
		personal_spaces = socialnavigationgaussian_proxy->getPersonalSpace(persons, 0.9, false);
		if (personal_spaces.size() == 0)
			return;
		if (personal_spaces[0].size() == 0)
			return;
	}
	catch (...)
	{
		qDebug() << __FUNCTION__ << "Error reading personal space from SocialGaussian";
	}

	std::vector<QPolygonF> new_polys;
	for (auto &&poly : personal_spaces)
	{
		QPolygonF polygon;
		for (auto &&p : poly)
			polygon << QPointF(p.x * 1000.f, p.z * 1000.f);
		new_polys.push_back(polygon);
	}
	updateFreeSpaceMap(new_polys);
}

void SpecificWorker::updateFreeSpaceMap(const std::vector<QPolygonF> &new_polys)
{
	// First remove polygons from last iteration respecting cell occupied by furniture
	for (auto &&p : human_poly)
	{
		grid.markAreaInGridAs(p->polygon(), true);
		scene.removeItem(p);
	}
	// Then insert new_polys
	human_poly.clear();
	for (auto &&p : new_polys)
	{
		human_poly.push_back(scene.addPolygon(p, QColor("LightGreen") /*,QBrush(QColor("LightGreen"))*/));
		grid.markAreaInGridAs(p, false);
	}
}

float SpecificWorker::degreesToRadians(const float angle_)
{
	float angle = angle_ * 2 * M_PI / 360;
	if (angle > M_PI)
		return angle - M_PI * 2;
	else if (angle < -M_PI)
		return angle + M_PI * 2;
	else
		return angle;
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

// // // external forces
// 	// // we need the minimun distance from each point to the obstacle(s).
// 	// // we compute the shortest laser ray to each point in the path
// 	std::vector<QVector2D> eforces;
// 	static std::vector<QGraphicsLineItem*> lforces;

// 	//remove graphic lines
// 	for(auto &&f: lforces)
// 		scene.removeItem(f);
// 	lforces.clear();
// 	// compute minimun distances to each point within the laser field
// 	for( auto &&p : lpath)
// 	{
// 		QVector2D force(0,0), f_force(0,0);
// 		if(p->data(0).value<bool>() == true) // if visible (computed before) then it is inside laser field
// 		{
// 			std::vector<std::tuple<float, QVector2D>> distances;
// 			// apply to all laser points a functor to compute the distances to point in path (p)
// 			std::transform(std::begin(lData), std::end(lData), std::back_inserter(distances), [p, this](auto &l)
// 				{
// 					// laser ray tip
// 					auto t = QPointF(l.dist*sin(l.angle), l.dist*cos(l.angle)); //can be optimized
// 					QVector2D tip(robot->mapToScene(t));
// 					if(QVector2D(t).length() < MAX_LASER_DIST)
// 					{
// 						// subtract ball radius  200 = robot size / 2
// 						float dist = std::min((QVector2D(p->pos()) - tip).length()-200, 0.f);
// 						return std::make_tuple(dist, QVector2D(p->pos()) - tip);
// 					}
// 					else
// 						return std::make_tuple(MAX_LASER_DIST, QVector2D(p->pos()));
// 				});
// 			// compute min distance
// 			auto min = std::min_element(std::begin(distances), std::end(distances), [](auto &a, auto &b){return std::get<float>(a) < std::get<float>(b);});
// 			// inverse raw force is min distante
// 			force = std::get<QVector2D>(*min);
// 			float mag = force.length()-200;

// 			// compute linear inverse law  y = -1/4 x + 200
// 			if(mag > X_ZERO) mag = X_ZERO;
// 			mag  = -(Y_ZERO/X_ZERO)*mag + Y_ZERO;
// 			f_force = mag * force.normalized();

// 			auto arrow = new QGraphicsLineItem(QLineF( QPointF(0,0), -p->mapFromScene(1.1*(p->pos()+f_force.toPointF()))), p);
// 			auto point = new QGraphicsEllipseItem(0,0,30,30, arrow);
// 			point->setBrush(QBrush(QColor("DarkGreen")));
// 			arrow->setPen(QPen(QBrush(QColor("DarkGreen")),10));
// 			point->setPos(arrow->line().p2());
// 			lforces.push_back(arrow);
// 			eforces.push_back(f_force);

// 			const float KE = 1;
// 			const float KI = 0.3;
// 			const auto &idelta = KI * iforce.toPointF();
// 			const auto &edelta = KE * f_force.toPointF();
// 			const auto force =  idelta + edelta;
// 			point->setPos( point->pos() + force );
// 		}
// 	}




// void SpecificWorker::computeForcesJacobian(const std::vector<QGraphicsEllipseItem *> &path, const std::vector<LData> &lData)
// {
// 	if (path.size() < 3)
// 		return;

// 	//convert laser polar coordinates to cartesian
// 	std::vector<QVector2D> laser_cart;
// 	QPolygonF laser_poly;
// 	for (const auto &l : lData)
// 	{
// 		QPointF p(robot->mapToScene(l.dist * sin(l.angle), l.dist * cos(l.angle)));
// 		laser_cart.push_back(QVector2D(p));
// 		laser_poly << p;
// 	}

// 	// Go through points using a sliding windows of 3
// 	for (auto group : iter::sliding_window(path, 3))
// 	{
// 		if (group.size() < 3)
// 			break; // break if too short

// 		auto p1 = QVector2D(group[0]->pos());
// 		auto p2 = QVector2D(group[1]->pos());
// 		auto p3 = QVector2D(group[2]->pos());
// 		auto p = group[1];

// 		if (isVisible(p) == false) // if not visible (computed before) continue
// 			continue;

// 		// internal curvature forces on p2
// 		QVector2D iforce = ((p1 - p2) / (p1 - p2).length() + (p3 - p2) / (p3 - p2).length());

// 		// EXTERNAL FORCES
// 		// We need the minimun distance from each point to the obstacle(s). we compute the shortest laser ray to each point in the path
// 		// compute minimun distances to each point within the laser field
// 		std::vector<float> distances, mx, px, my, py;
// 		for (const auto &l : laser_cart)
// 		{
// 			// auto lcart = QPointF(l.dist*sin(l.angle), l.dist*cos(l.angle));  				// can be optimized out
// 			// QVector2D lcart_world(robot->mapToScene(lcart));								// transform to scene RefS
// 			// compute distante from laser tip to point minus RLENGTH/2 or 0 and keep it positive
// 			distances.push_back((l - p2).length());
// 			mx.push_back((l - (p2 - QVector2D(DELTA_H, 0))).length());
// 			px.push_back((l - (p2 + QVector2D(DELTA_H, 0))).length());
// 			my.push_back((l - (p2 - QVector2D(0, DELTA_H))).length());
// 			py.push_back((l - (p2 + QVector2D(0, DELTA_H))).length());
// 		}
// 		float min_dist = *std::min_element(std::begin(distances), std::end(distances));

// 		float min_mx = *std::min_element(std::begin(mx), std::end(mx));
// 		float min_px = *std::min_element(std::begin(px), std::end(px));
// 		float min_my = *std::min_element(std::begin(my), std::end(my));
// 		float min_py = *std::min_element(std::begin(py), std::end(py));

// 		// space interval to compute the derivative. Related to to robot's size
// 		QVec jacobian = QVec::vec2(min_mx - min_px, min_my - min_py) * (T)(1.f / (2.f * DELTA_H));

// 		// repulsion force is computed in the direction of maximun laser-point distance variation and scaled so it is 0 is beyond FORCE_DISTANCE_LIMIT and FORCE_DISTANCE_LIMIT if w1.minDist.
// 		auto final_dist = std::min(FORCE_DISTANCE_LIMIT, min_dist);
// 		QVec repulsion_force = jacobian * (T)(FORCE_DISTANCE_LIMIT - final_dist);

// 		//Remove tangential component of repulsion force by projecting on line tangent to path (base_line)
// 		// QVec base_line = (p1 - p3).normalize();
// 		// const QVec itangential = (repulsion_force * base_line) * base_line;
// 		// repulsion_force = repulsion_force - itangential;

// 		// add the forces and move the point using KE y KI defined in .h
// 		//qDebug() << min_dist << final_dist << min_mx << min_px <<  min_my << min_py << "jac" << jacobian << iforce << repulsion_force << jacobian;
// 		QVector2D total = (KI * iforce) + (-KE * QVector2D(repulsion_force.x(), repulsion_force.y()));
// 		if (total.length() > 68)
// 			total = 68 * total.normalized();

// 		// move node only if does not exit the laser polygon
// 		if (laser_poly.containsPoint(p->pos() + total.toPointF(), Qt::OddEvenFill))
// 			p->setPos(p->pos() + total.toPointF());

// 		// remove old arrows and edges
// 		for (const auto &pp : path)
// 			for (auto &&child : pp->childItems())
// 				scene.removeItem(child);
// 		// create new ones
// 		auto arrow = new QGraphicsLineItem(QLineF(QPointF(0, 0), p->mapFromScene(1.1 * (p->pos() + QPointF(repulsion_force.x(), repulsion_force.y())))), p);
// 		auto point = new QGraphicsEllipseItem(0, 0, 40, 40, arrow);
// 		point->setBrush(QBrush(QColor("DarkGreen")));
// 		arrow->setPen(QPen(QBrush(QColor("DarkGreen")), 10));
// 		point->setPos(arrow->line().p2());
// 	}

// 	first->setPos(robot_nose->mapToScene(QPointF(0, 0)));
// 	last->setPos(target->pos());

// 	//Draw edges
// 	for (auto &&pair : iter::sliding_window(path, 2))
// 	{
// 		for (auto &&child : pair[0]->childItems())
// 		{
// 			auto line = qgraphicsitem_cast<QGraphicsLineItem *>(child);
// 			if (line)
// 				scene.removeItem(line);
// 		}
// 		auto line = new QGraphicsLineItem(QLineF(QPointF(0, 0), pair[0]->mapFromScene(pair[1]->pos())), pair[0]);
// 		line->setPen(QPen(QColor("Red"), 20));
// 	}
// }
