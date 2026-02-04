// Traverse.cpp
//
// Create and print a sorted list of all the paths specified
// on the command line, traversing any directories to add
// their contents using a pool of worker threads.  Ignore
// any non-existent paths.
//
// Compile with g++ --std=c++17 Traverse.cpp -pthread -o Traverse

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>

#include <iostream>
#include <vector>
#include <queue>
#include <algorithm>

using namespace std;

queue<string> q;
vector<string> found_paths;

int ThreadCount = 0;
int sleeping_threads = 0;
bool shutdown = false;

pthread_mutex_t q_lock;
pthread_mutex_t paths_lock;
pthread_mutex_t sleeping_lock;
pthread_cond_t cond;
//		YOUR CODE HERE

// Get the next path to be traversed from the work queue
// shared among all the threads, sleeping if the queue is
// empty. If this is the last thread going to sleep, signal
// that all the work is done.

string GetWork()
{
	pthread_mutex_lock(&q_lock);
	while (q.empty() && !shutdown)
	{
		pthread_mutex_lock(&sleeping_lock);
		sleeping_threads++;

		if (sleeping_threads == ThreadCount)
		{
			shutdown = true;
			pthread_cond_broadcast(&cond);
			pthread_mutex_unlock(&sleeping_lock);
			break;
		}

		pthread_mutex_unlock(&sleeping_lock);
		pthread_cond_wait(&cond, &q_lock);

		pthread_mutex_lock(&sleeping_lock);
		sleeping_threads--;
		pthread_mutex_unlock(&sleeping_lock);
	}

	if (shutdown && q.empty())
	{
		pthread_mutex_unlock(&q_lock);
		return "";
	}

	string return_val = q.front();
	q.pop();
	pthread_mutex_unlock(&q_lock);
	return return_val;
}

// Add  work to the queue and signal that there's work available.

void AddWork(string path)
{
	pthread_mutex_lock(&q_lock);
	q.push(path);
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&q_lock);
}

// Add a new path to the list all those that have been found,

void AddPath(const string &path)
{
	pthread_mutex_lock(&paths_lock);
	found_paths.emplace_back(path);
	pthread_mutex_unlock(&paths_lock);
}

bool DotName(const char *name)
{
	return name[0] == '.' &&
		   (name[1] == 0 || name[1] == '.' && name[2] == 0);
}

// Traverse a path.  If it exi	sts, add it to the list of paths that
// have been found.  If it's a directory, add any children other than
// . and .. to the work queue.  If it doesn't exist, ignore it.

void *Traverse(string pathname)
{
	struct stat st;
	if (stat(pathname.c_str(), &st) != 0)
		return nullptr;

	AddPath(pathname);

	if (S_ISDIR(st.st_mode))
	{
		DIR *dir = opendir(pathname.c_str());
		if (!dir)
			return nullptr;

		struct dirent *entry;
		while ((entry = readdir(dir)) != nullptr)
		{

			if (!DotName(entry->d_name))
			{
				string child = pathname;
				if (child.back() != '/')
					child += "/";
				child += entry->d_name;

				AddWork(child);
			}
		}
		closedir(dir);
	}

	return nullptr;
}

// Each worker thread simply loops, grabbing the next item
// on the work queue and traversing it.

void *WorkerThread(void *arg)
{
	while (true)
	{
		string path = GetWork();
		if (path == "" && shutdown)
			break;
		Traverse(path);
	}
	// Never reached.
	return nullptr;
}

// main() should do the following.
//
// 1. Initialize the locks.
// 2. Iterate over the argv pathnames, adding them to the work queue.
// 3. Create the specified number of workers.
// 4. Sleep until the work has finished.
// 5. Sort the paths found vector.
// 6  Print the list of paths.

int main(int argc, char **argv)
{
	if (argc < 3 || (ThreadCount = atoi(argv[1])) == 0)
	{
		cerr << "Usage: Traverse <number of workers> <list of pathnames>" << endl
			 << "Number of workers must be greater than 0." << endl
			 << "Invalid paths are ignored." << endl;
		return 1;
	}

	for (int i = 2; i < argc; i++)
	{
		q.emplace(argv[i]);
	}

	vector<pthread_t> threads(ThreadCount);
	for (int i = 0; i < ThreadCount; i++)
	{
		pthread_create(&threads[i], nullptr, WorkerThread, nullptr);
	}

	for (int i = 0; i < ThreadCount; i++)
	{
		pthread_join(threads[i], nullptr);
	}

	sort(found_paths.begin(), found_paths.end());

	for (const string &s : found_paths)
	{
		cout << s << '\n';
	}

	return 0;
}
