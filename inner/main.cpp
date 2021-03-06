#include <iostream>
#include <thread>
#include <unordered_map>
#include <string>
#include <vector>
#include <mutex>
#include <random>
#include <future>
#include <chrono>
#include <shared_mutex>

#include "inner.h"
#include "nodes.h"

using namespace std::chrono_literals;

using NODEPtr = std::shared_ptr<NODE>;
using TRANSFORMPtr = std::shared_ptr<TRANSFORM>;
using JOINTPtr = std::shared_ptr<JOINT>;

////////////////////////////////////////////////////////////////////////////////////////////////////////

void readThread(const std::shared_ptr<Inner> &inner)
{
	std::random_device r;
	std::default_random_engine e1(r());
	std::vector<std::string> keys = inner->hash->keys();
	std::uniform_int_distribution<int> uniform_dist(0, keys.size()-1);

	while(true)
	{
		inner->printIter();
		std::this_thread::sleep_for(100ms);
	}
}
void writeThread(const std::shared_ptr<Inner> &inner)
{
	std::random_device r;
	std::default_random_engine e1(r());

	while(true)
	{
		std::vector<std::string> keys = inner->hash->keys();
		std::uniform_int_distribution<int> uniform_dist(0, keys.size()-1);
		auto target = keys[uniform_dist(e1)];
		auto node = inner->getNode<TRANSFORM>(target);
		if(node)
		{
			auto id = node->getId();
			std::this_thread::sleep_for(1ms);
			node->setId(id);
			std::this_thread::sleep_for(100ms);
		}
	}
}

void createThread(const std::shared_ptr<Inner> &inner)
{
	std::random_device r;
	std::default_random_engine e1(r());
	std::uniform_int_distribution<int> name_dist(0, 10000);

	while(inner->hash->size()<200)
	{
		std::cout << "THREAD: creating: size " << inner->hash->size() << std::endl;
		std::vector<std::string> keys = inner->hash->keys();
		std::uniform_int_distribution<int> uniform_dist(0, keys.size()-1);
		auto parent = keys[uniform_dist(e1)];
        std::string id = std::to_string(name_dist(e1));
        std::cout << "THREAD: creating id " << id << " trying to lock parent " << parent << std::endl;
        auto parentNode = inner->getNode<NODE>(parent);
        std::cout << "THREAD: creating id " << id << " with parent " << parent << std::endl;
        if(parentNode)
            try
            {	auto n = inner->newNode<TRANSFORM>(id, inner, parent); }
            catch(const std::exception &e)
            {	std::cout << e.what() << " " << parent << " Main::createThread - Error" << std::endl;}
        std::this_thread::sleep_for(300ms);
	}
}

void chainThread(const std::shared_ptr<Inner> &inner)
{
	std::random_device r;
	std::default_random_engine e1(r());
	std::uniform_int_distribution<int> chain_dist(1, 9);

	while(true)
	{
		std::vector<std::string> keys = inner->hash->keys();
		std::uniform_int_distribution<int> uniform_dist(0, keys.size()-1);
		auto id = keys[uniform_dist(e1)];
		auto running_id = id, parent_id=id;
		auto levelsUp = chain_dist(e1);
		auto levelsDown = chain_dist(e1);
		int cont = 0;
		do
		{
            auto node = inner->getNode<NODE>(running_id);
            if(node)
            {
                 parent_id = node->getParentId();
                 running_id = parent_id;
                 cont++;
            }
            else break;
		}
        while(parent_id != "" or cont>levelsUp);
		cont = 0;
		auto child_id = running_id;
		do
		{
            auto node = inner->getNode<NODE>(running_id);
            if(node)
            {
				if(node->getNumChildren()>0)
				{
					std::uniform_int_distribution<int> child_dist(0, node->getNumChildren()-1);
					child_id = node->getChildId(child_dist(e1));
					running_id = child_id;
                	cont++;
				}
				else
					break;
            }
        	else
                break;
		}
		while(child_id != "" or cont>levelsDown);
		std::cout << "THREAD transform: up " << levelsUp << " down "  << levelsDown << std::endl;
		std::this_thread::sleep_for(100ms);
	}
}

void deleteThread(const std::shared_ptr<Inner> &inner)
{
	std::random_device r;
	std::default_random_engine e1(r());

	while(true)
	{
		std::vector<std::string> keys = inner->hash->keys();
		std::uniform_int_distribution<int> uniform_dist(0, keys.size()-1);
		auto id = keys[uniform_dist(e1)];
		if(id == inner->getRootId())
			continue;
		std::cout << "D THREAD: deleting id " << id << std::endl;
        std::vector<std::string> l;
        try
        {
            inner->removeSubTree(id,l);
            std::cout << "D THREAD: deleted " << l.size() << " items:" << std::endl;
            for(auto&& n: l)
                std::cout << n << std::endl;
            std::cout << "---------end deleted----------" << std::endl;
        }
        catch(const std::exception &e)
        { std::cout << e.what() << std::endl;};

		std::this_thread::sleep_for(100ms);
	}
}

int main()
{
	std::cout << std::boolalpha;
	auto inner = std::make_shared<Inner>();

    try{
	inner->setRootId("root");
	inner->newNode<TRANSFORM>("t1", inner, "root");
	inner->newNode<TRANSFORM>("t2", inner, "root");
	inner->newNode<TRANSFORM>("t3", inner, "root");
	inner->newNode<TRANSFORM>("t4", inner, "root");

	inner->newNode<JOINT>("j1", inner, "t1");
	inner->newNode<JOINT>("j2", inner, "t2");
	inner->newNode<JOINT>("j3", inner, "t3");

	inner->newNode<TRANSFORM>("t5", inner, "t4");
	inner->newNode<TRANSFORM>("t6", inner, "t4");
    }
    catch(const std::exception &e){ std::cout << e.what() << std::endl;   }

	std::cout << "---------------Created, now printing--------------" << std::endl;
	inner->printIter();

	std::cout << "----------------Get Node---------------------" << std::endl;
// 	auto j = inner->getNode<JOINT>("j1");
// 	j->print();
// 	j.reset();
// 	auto t = inner->getNode<TRANSFORM>("t1");
// 	t->print();
// 	t.reset();

	std::cout << "----------------Delete Node---------------------" << std::endl;
	//std::cout << "COUNTER " << inner->getNode<NODE>("t5").use_count() << std::endl;
	//auto node = inner->getNode<NODE>("t5");
	//std::cout << "COUNTER " << node.use_count() << std::endl;
	//node->markForDelete();

	//inner->deleteNode("t5");
	//node.reset();
	//std::cout << "asdfa " << node.use_count() << std::endl;
	//inner->hash.erase("t5");

        ///Check removeTree
//         std::vector<std::string> l;
//         inner->removeSubTree("kk",l);
//         inner->printIter();
 //    exit(-1);
	/////////////////////
        //Check mutex
//         auto t1 = inner->getNode<TRANSFORM>("root");
//         //std::cout << "First access" << t->getId() << t->lock() << std::endl;
//         std::async(std::launch::async, [inner]
//         {   if( auto t2 = inner->getNode<TRANSFORM>("root"))
//             std::cout << "Second access" << t2->getId() << std::endl;
//         });
//        exit(-1);


	std::cout << "-----------threads-------------------------" << std::endl;
	std::vector<int> RN = {}, WN = {0,1,2}, CN = {0,1}, TN = {0,1,2,3,4,5,6}, DN = {};
	std::future<void> threadsR[RN.size()], threadsW[WN.size()], threadsC[CN.size()], threadsT[TN.size()], threadsD[DN.size()];

	for (auto&& i : RN)
		threadsR[i] = std::async(std::launch::async, readThread, inner);

 	for (auto&& i : WN)
 		threadsW[i] = std::async(std::launch::async, writeThread, inner);

	for (auto&& i : CN)
		threadsC[i] = std::async(std::launch::async, createThread, inner);

	for (auto&& i : TN)
		threadsT[i] = std::async(std::launch::async, chainThread, inner);

	for (auto&& i : DN)
		threadsD[i] = std::async(std::launch::async, deleteThread, inner);

	for (auto&& i: DN)
		threadsD[i].wait();
	for (auto&& i: CN)
		threadsC[i].wait();
	for (auto&& i: RN)
		threadsR[i].wait();
	for (auto&& i: WN)
		threadsW[i].wait();
	for (auto&& i: TN)
		threadsT[i].wait();

}
