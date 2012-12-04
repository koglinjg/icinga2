/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012 Icinga Development Team (http://www.icinga.org/)        *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#include "i2-base.h"

#ifndef _WIN32
#	include <ltdl.h>
#endif /* _WIN32 */

using namespace icinga;

Application *Application::m_Instance = NULL;
bool Application::m_ShuttingDown = false;
bool Application::m_Debugging = false;
boost::thread::id Application::m_MainThreadID;
String Application::m_PrefixDir;
String Application::m_LocalStateDir;
String Application::m_PkgLibDir;

/**
 * Constructor for the Application class.
 */
Application::Application(const Dictionary::Ptr& serializedUpdate)
	: DynamicObject(serializedUpdate), m_PidFile(NULL)
{
	if (!IsLocal())
		throw_exception(runtime_error("Application objects must be local."));

#ifdef _WIN32
	/* disable GUI-based error messages for LoadLibrary() */
	SetErrorMode(SEM_FAILCRITICALERRORS);

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(1, 1), &wsaData) != 0)
		throw_exception(Win32Exception("WSAStartup failed", WSAGetLastError()));
#endif /* _WIN32 */

	char *debugging = getenv("_DEBUG");
	m_Debugging = (debugging && strtol(debugging, NULL, 10) != 0);

#ifdef _WIN32
	if (IsDebuggerPresent())
		m_Debugging = true;
#endif /* _WIN32 */

	assert(m_Instance == NULL);
	m_Instance = this;
}

/**
 * Destructor for the application class.
 */
Application::~Application(void)
{
	m_Instance = NULL;

	m_ShuttingDown = true;

#ifdef _WIN32
	WSACleanup();
#endif /* _WIN32 */

	ClosePidFile();
}

/**
 * Retrieves a pointer to the application singleton object.
 *
 * @returns The application object.
 */
Application::Ptr Application::GetInstance(void)
{
	if (m_Instance)
		return m_Instance->GetSelf();
	else
		return Application::Ptr();
}

/**
 * Processes events for registered sockets and timers and calls whatever
 * handlers have been set up for these events.
 */
void Application::RunEventLoop(void)
{
#ifdef _DEBUG
	double nextProfile = 0;
#endif /* _DEBUG */

	/* Start the system time watch thread. */
	thread t(&Application::TimeWatchThreadProc);
	t.detach();

	while (!m_ShuttingDown) {
		Object::ClearHeldObjects();

		double sleep = Timer::ProcessTimers();

		if (m_ShuttingDown)
			break;

		Event::ProcessEvents(boost::posix_time::milliseconds(sleep * 1000));

		DynamicObject::FinishTx();
		DynamicObject::BeginTx();

#ifdef _DEBUG
		if (nextProfile < Utility::GetTime()) {
			stringstream msgbuf;
			msgbuf << "Active objects: " << Object::GetAliveObjectsCount();
			Logger::Write(LogInformation, "base", msgbuf.str());

			Object::PrintMemoryProfile();

			nextProfile = Utility::GetTime() + 15.0;
		}
#endif /* _DEBUG */
	}
}

/**
 * Watches for changes to the system time. Adjusts timers if necessary.
 */
void Application::TimeWatchThreadProc(void)
{
	double lastLoop = Utility::GetTime();

	for (;;) {
		Utility::Sleep(5);

		double now = Utility::GetTime();
		double timeDiff = lastLoop - now;

		if (abs(timeDiff) > 15) {
			/* We made a significant jump in time. */
			stringstream msgbuf;
			msgbuf << "We jumped "
			       << (timeDiff < 0 ? "forward" : "backward")
			       << " in time: " << abs(timeDiff) << " seconds";
			Logger::Write(LogInformation, "base", msgbuf.str());

			/* in addition to rescheduling the timers this
			 * causes the event loop to wake up thereby
			 * solving the problem that timed_wait()
			 * uses an absolute timestamp for the timeout */
			Event::Post(boost::bind(&Timer::AdjustTimers,
			    -timeDiff));
		}

		lastLoop = now;
	}
}

/**
 * Signals the application to shut down during the next
 * execution of the event loop.
 */
void Application::RequestShutdown(void)
{
	m_ShuttingDown = true;
}

/**
 * Terminates the application.
 */
void Application::Terminate(int exitCode)
{
	_exit(exitCode);
}

/**
 * Retrieves the full path of the executable.
 *
 * @param argv0 The first command-line argument.
 * @returns The path.
 */
String Application::GetExePath(const String& argv0)
{
	String executablePath;

#ifndef _WIN32
	char buffer[MAXPATHLEN];
	if (getcwd(buffer, sizeof(buffer)) == NULL)
		throw_exception(PosixException("getcwd failed", errno));
	String workingDirectory = buffer;

	if (argv0[0] != '/')
		executablePath = workingDirectory + "/" + argv0;
	else
		executablePath = argv0;

	bool foundSlash = false;
	for (size_t i = 0; i < argv0.GetLength(); i++) {
		if (argv0[i] == '/') {
			foundSlash = true;
			break;
		}
	}

	if (!foundSlash) {
		const char *pathEnv = getenv("PATH");
		if (pathEnv != NULL) {
			vector<String> paths;
			boost::algorithm::split(paths, pathEnv, boost::is_any_of(":"));

			bool foundPath = false;
			BOOST_FOREACH(String& path, paths) {
				String pathTest = path + "/" + argv0;

				if (access(pathTest.CStr(), X_OK) == 0) {
					executablePath = pathTest;
					foundPath = true;
					break;
				}
			}

			if (!foundPath) {
				executablePath.Clear();
				throw_exception(runtime_error("Could not determine executable path."));
			}
		}
	}

	if (realpath(executablePath.CStr(), buffer) == NULL)
		throw_exception(PosixException("realpath failed", errno));

	return buffer;
#else /* _WIN32 */
	char FullExePath[MAXPATHLEN];

	if (!GetModuleFileName(NULL, FullExePath, sizeof(FullExePath)))
		throw_exception(Win32Exception("GetModuleFileName() failed", GetLastError()));

	return FullExePath;
#endif /* _WIN32 */
}

/**
 * Retrieves the debugging mode of the application.
 *
 * @returns true if the application is being debugged, false otherwise
 */
bool Application::IsDebugging(void)
{
	return m_Debugging;
}

/**
 * Checks whether we're currently on the main thread.
 *
 * @returns true if this is the main thread, false otherwise
 */
bool Application::IsMainThread(void)
{
	return (boost::this_thread::get_id() == m_MainThreadID);
}

/**
 * Sets the main thread to the currently running thread.
 */
void Application::SetMainThread(void)
{
	m_MainThreadID = boost::this_thread::get_id();
}

#ifndef _WIN32
/**
 * Signal handler for SIGINT. Prepares the application for cleanly
 * shutting down during the next execution of the event loop.
 *
 * @param signum The signal number.
 */
void Application::SigIntHandler(int signum)
{
	assert(signum == SIGINT);

	Application::Ptr instance = Application::GetInstance();

	if (!instance)
		return;

	instance->RequestShutdown();

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	sigaction(SIGINT, &sa, NULL);
}
#else /* _WIN32 */
/**
 * Console control handler. Prepares the application for cleanly
 * shutting down during the next execution of the event loop.
 */
BOOL WINAPI Application::CtrlHandler(DWORD type)
{
	Application::Ptr instance = Application::GetInstance();

	if (!instance)
		return TRUE;

	instance->GetInstance()->RequestShutdown();

	SetConsoleCtrlHandler(NULL, FALSE);
	return TRUE;
}
#endif /* _WIN32 */

/**
 * Runs the application.
 *
 * @param argc The number of arguments.
 * @param argv The arguments that should be passed to the application.
 * @returns The application's exit code.
 */
int Application::Run(int argc, char **argv)
{
	int result;

#ifndef _WIN32
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = &Application::SigIntHandler;
	sigaction(SIGINT, &sa, NULL);

	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, NULL);
#else
	SetConsoleCtrlHandler(&Application::CtrlHandler, TRUE);
#endif /* _WIN32 */

	m_Arguments.clear();
	for (int i = 0; i < argc; i++)
		m_Arguments.push_back(String(argv[i]));

	DynamicObject::BeginTx();

	result = Main(m_Arguments);

	DynamicObject::FinishTx();
	DynamicObject::DeactivateObjects();

	return result;
}

/**
 * Grabs the PID file lock and updates the PID. Terminates the application
 * if the PID file is already locked by another instance of the application.
 *
 * @param filename The name of the PID file.
 */
void Application::UpdatePidFile(const String& filename)
{
	ClosePidFile();

	/* There's just no sane way of getting a file descriptor for a
	 * C++ ofstream which is why we're using FILEs here. */
	m_PidFile = fopen(filename.CStr(), "w");

	if (m_PidFile == NULL)
		throw_exception(runtime_error("Could not open PID file '" + filename + "'"));

#ifndef _WIN32
	if (flock(fileno(m_PidFile), LOCK_EX | LOCK_NB) < 0) {
		ClosePidFile();

		Logger::Write(LogCritical, "base",
		    "Another instance of the application is "
		    "already running. Remove the '" + filename + "' file if "
		    "you're certain that this is not the case.");
		Terminate(EXIT_FAILURE);
	}
#endif /* _WIN32 */

	fprintf(m_PidFile, "%d", Utility::GetPid());
	fflush(m_PidFile);
}

/**
 * Closes the PID file. Does nothing if the PID file is not currently open.
 */
void Application::ClosePidFile(void)
{
	if (m_PidFile != NULL)
		fclose(m_PidFile);

	m_PidFile = NULL;
}

/**
 * Retrieves the path of the installation prefix.
 *
 * @returns The path.
 */
String Application::GetPrefixDir(void)
{
	if (m_PrefixDir.IsEmpty())
		return ".";
	else
		return m_PrefixDir;
}

/**
 * Sets the path for the installation prefix.
 *
 * @param path The new path.
 */
void Application::SetPrefixDir(const String& path)
{
	m_PrefixDir = path;
}

/**
 * Retrieves the path for the local state dir.
 *
 * @returns The path.
 */
String Application::GetLocalStateDir(void)
{
	if (m_LocalStateDir.IsEmpty())
		return "./var";
	else
		return m_LocalStateDir;
}

/**
 * Sets the path for the local state dir.
 *
 * @param path The new path.
 */
void Application::SetLocalStateDir(const String& path)
{
	m_LocalStateDir = path;
}

/**
 * Retrives the path for the package lib dir.
 *
 * @returns The path.
 */
String Application::GetPkgLibDir(void)
{
	if (m_PkgLibDir.IsEmpty())
		return ".";
	else
		return m_PkgLibDir;
}

/**
 * Sets the path for the package lib dir.
 *
 * @param path The new path.
 */
void Application::SetPkgLibDir(const String& path)
{
	m_PkgLibDir = path;
}
