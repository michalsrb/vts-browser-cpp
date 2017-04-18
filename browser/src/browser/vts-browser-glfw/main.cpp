#include <cstdio>
#include <cstdlib>
#include <vts/map.hpp>
#include "mainWindow.hpp"
#include "dataThread.hpp"
#include "threadName.hpp"
#include <GLFW/glfw3.h>

void errorCallback(int error, const char* description)
{
    fprintf(stderr, "GLFW error: %s\n", description);
}

void usage(char *argv[])
{
    printf("Usage: %s [options] [--] <url> [url...]\n", argv[0]);
}

int main(int argc, char *argv[])
{
    // release build -> catch exceptions and print them to stderr
    // debug build -> let the debugger handle the exceptions
#ifdef NDEBUG
    try
    {
#endif
        int firstUrl = argc;
        for (int i = 1; i < argc; i++)
        {
            if (argv[i][0] == '-')
            {
                if (argv[i][1] == '-')
                {
                    firstUrl = i + 1;
                    break;
                }
                // todo handle options
            }
            else
            {
                firstUrl = i;
                break;
            }
        }
        if (firstUrl >= argc)
        {
            usage(argv);
            return 3;
        }
    
        glfwSetErrorCallback(&errorCallback);
        if (!glfwInit())
            return 2;
    
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
        glfwWindowHint(GLFW_STENCIL_BITS, 0);
        glfwWindowHint(GLFW_DEPTH_BITS, 32);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifndef NDEBUG
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);
#endif
    
        {
            vts::MapFoundationOptions options;
            vts::MapFoundation map(options);
            map.setMapConfigPath(argv[firstUrl]);
            MainWindow main;
            for (int i = firstUrl; i < argc; i++)
                main.mapConfigPaths.push_back(argv[i]);
            DataThread data(main.window);
            main.map = &map;
            data.map = &map;
            setThreadName("main");
            main.run();
        }
    
        glfwTerminate();
        return 0;
#ifdef NDEBUG
    }
    catch(const std::exception &e)
    {
        fprintf(stderr, "Exception: %s\n", e.what());
        return 1;
    }
    catch(const char *e)
    {
        fprintf(stderr, "Exception: %s\n", e);
        return 1;
    }
    catch(...)
    {
        fprintf(stderr, "Unknown exception.\n");
        return 1;
    }
#endif
}