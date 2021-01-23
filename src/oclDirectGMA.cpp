/* Header includes */
#ifdef _WIN32
#include <windows.h>
#elif defined __MACH__
#include <mach/mach_time.h>
#else
#include <sys/time.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "CL/cl.h"
#include "CL/cl_ext.h"
#include "utils.h"

/* Defines to control sample behavior */
#define USE_CL_EXTENSION				/* <-- Define to enable use of CL_AMD_BUS_ADDRESSABLE_MEMORY extension. Disable to use system memory for copying data */
#ifdef USE_CL_EXTENSION
#define USE_EVENTS_TO_PROFILE			/* <-- Define to enable use of OpenCL profiling events to measure throughput. Disable to use Windows performance counters to measure throughput */
#define TRANSFER_COUNT	5000			/* <-- Define number of iterations for the buffer transfer via the extension */
#else
#define USE_MAP_UNMAP_BUFFERS			/* <-- Define to use OpenCL map/unmap APIs for buffer transfers via system memory. Disable to use OpenCL read/write APIs for buffer transfers */
#define TRANSFER_COUNT	500				/* <-- Define number of iterations for the buffer transfer via system memory */
#endif

/* Sample's handle definition */
typedef struct oclDirectGMA_context
{
    cl_platform_id     platform;
    cl_device_id       devices[2];
    cl_context         contexts[2];
    cl_command_queue   cmdQueues[2];

    cl_uint            bufSize;
    cl_mem             srcBuff;
    cl_mem             dstBuff;
    cl_mem             extDstBuff;
    cl_uint            *inputArr;
    cl_uint            *outputArr;
} oclDirectGMA_context;

/* DirectGMA functions - init */
bool oclDirectGMA_init(oclDirectGMA_context **extHandle, unsigned int dstDevice, unsigned int transferSize)
{
    oclDirectGMA_context               *handle = *extHandle;
    cl_uint                            numPlatforms = 0;
    cl_uint                            numDevices = 0;
    cl_int                             status = CL_SUCCESS;
    clEnqueueMakeBuffersResidentAMD_fn clEnqueueMakeBuffersResidentAMD = NULL;
    cl_bus_address_amd                 dstBusAddress;

    /* Init handle */
    handle->platform = NULL;
    handle->bufSize = transferSize;
    handle->srcBuff = NULL;
    handle->dstBuff = NULL;
    handle->extDstBuff = NULL;
    handle->inputArr = NULL;
    handle->outputArr = NULL;
    handle->devices[0] = handle->devices[1] = NULL;
    handle->contexts[0] = handle->contexts[1] = NULL;
    handle->cmdQueues[0] = handle->cmdQueues[1] = NULL;

    /* Get numPlatforms */
    status = clGetPlatformIDs(0, NULL, &numPlatforms);
    CHECK_RESULT(numPlatforms == 0, "clGetPlatformIDs failed. Error code = %d", status);

    /* Get AMD platform */
    for (unsigned int i = 1; i < numPlatforms+1; i++)
    {
        size_t param_size = 0;
        char *platformVendor = NULL;
        status = clGetPlatformIDs(i, &handle->platform, NULL);
        CHECK_RESULT(status != CL_SUCCESS, "clGetPlatformIDs failed. Error code = %d", status);
        status = clGetPlatformInfo(handle->platform, CL_PLATFORM_VENDOR, 0, NULL, &param_size);
        CHECK_RESULT(status != CL_SUCCESS, "clGetPlatformInfo failed. Error code = %d", status);
        platformVendor = (char*)malloc(param_size);
        CHECK_RESULT(platformVendor == NULL, "Memory allocation failed: platformVendor");
        status = clGetPlatformInfo(handle->platform, CL_PLATFORM_VENDOR, param_size, platformVendor, NULL);
        CHECK_RESULT(status != CL_SUCCESS, "clGetPlatformInfo failed. Error code = %d", status);
        if (strstr(platformVendor, "Advanced Micro Devices, Inc.") != NULL)
        {
            free(platformVendor);
            break;
        }
        else
        {
            free(platformVendor);
            handle->platform = NULL;
        }
    }
    CHECK_RESULT(handle->platform == NULL, "AMD platform not found");

    /* Get num GPUs on platform */
    status = clGetDeviceIDs(handle->platform, CL_DEVICE_TYPE_GPU, 0, NULL, &numDevices);
    CHECK_RESULT(status != CL_SUCCESS, "clGetDeviceIDs failed. Error code = %d", status);
    CHECK_RESULT(numDevices < 2, "Two GPUs required for the test");

    /* Get list of GPUs on platform */
    status = clGetDeviceIDs(handle->platform, CL_DEVICE_TYPE_GPU, numDevices, handle->devices, 0);
    CHECK_RESULT(status != CL_SUCCESS, "clGetDeviceIDs failed. Error code = %d", status);
    if (dstDevice) {
        cl_device_id temp = handle->devices[0];
        handle->devices[0] = handle->devices[1];
        handle->devices[1] = temp;
    }

#ifdef USE_CL_EXTENSION
    /* Get device extensions for device 0 */
    size_t param_size = 0;
    char* strExtensions = NULL;
    status = clGetDeviceInfo(handle->devices[0], CL_DEVICE_EXTENSIONS, 0, 0, &param_size);
    CHECK_RESULT(status != CL_SUCCESS, "clGetDeviceInfo failed. Error code = %d", status);
    strExtensions = (char*)malloc(param_size);
    CHECK_RESULT(strExtensions == NULL, "Memory allocation failed: strExtensions");
    status = clGetDeviceInfo(handle->devices[0], CL_DEVICE_EXTENSIONS, param_size, strExtensions, 0);
    CHECK_RESULT(status != CL_SUCCESS, "clGetDeviceInfo failed. Error code = %d", status);
    CHECK_RESULT(strstr(strExtensions, "cl_amd_bus_addressable_memory")==0, "cl_amd_bus_addressable_memory extension is not enabled on GPU 0");
    free(strExtensions);

    /* Get device extensions for device 1 */
    status = clGetDeviceInfo(handle->devices[1], CL_DEVICE_EXTENSIONS, 0, 0, &param_size);
    CHECK_RESULT(status != CL_SUCCESS, "clGetDeviceInfo failed. Error code = %d", status);
    strExtensions = (char*)malloc(param_size);
    CHECK_RESULT(strExtensions == NULL, "Memory allocation failed: strExtensions");
    status = clGetDeviceInfo(handle->devices[1], CL_DEVICE_EXTENSIONS, param_size, strExtensions, 0);
    CHECK_RESULT(status != CL_SUCCESS, "clGetDeviceInfo failed. Error code = %d", status);
    CHECK_RESULT(strstr(strExtensions, "cl_amd_bus_addressable_memory")==0, "cl_amd_bus_addressable_memory extension is not enabled on GPU 1");
    free(strExtensions);

    /* Load required extensions */
    clEnqueueMakeBuffersResidentAMD = (clEnqueueMakeBuffersResidentAMD_fn)clGetExtensionFunctionAddressForPlatform(handle->platform, "clEnqueueMakeBuffersResidentAMD");
    CHECK_RESULT(clEnqueueMakeBuffersResidentAMD == NULL, "clGetExtensionFunctionAddressForPlatform returned NULL for clEnqueueMakeBuffersResidentAMD");
#endif

    /* Create context for both devices */
    cl_context_properties props[3] = {CL_CONTEXT_PLATFORM, (cl_context_properties)handle->platform, 0};
    handle->contexts[0] = clCreateContext(props, 1, &handle->devices[0], 0, 0, &status);
    CHECK_RESULT(handle->contexts[0] == 0, "clCreateContext failed. Error code = %d", status);
    handle->contexts[1] = clCreateContext(props, 1, &handle->devices[1], 0, 0, &status);
    CHECK_RESULT(handle->contexts[1] == 0, "clCreateContext failed. Error code = %d", status);

    /* Create command queue for both devices */
#ifdef USE_EVENTS_TO_PROFILE
	cl_queue_properties props1[3] = { CL_QUEUE_PROPERTIES, CL_QUEUE_PROFILING_ENABLE, 0 };
	handle->cmdQueues[0] = clCreateCommandQueueWithProperties(handle->contexts[0], handle->devices[0], props1, &status);
    CHECK_RESULT(handle->cmdQueues[0] == 0, "clCreateCommandQueue failed. Error code = %d", status);
    handle->cmdQueues[1] = clCreateCommandQueueWithProperties(handle->contexts[1], handle->devices[1], props1, &status);
    CHECK_RESULT(handle->cmdQueues[1] == 0, "clCreateCommandQueue failed. Error code = %d", status);
#else
    handle->cmdQueues[0] = clCreateCommandQueue(handle->contexts[0], handle->devices[0], 0, &status);
    CHECK_RESULT(handle->cmdQueues[0] == 0, "clCreateCommandQueue failed. Error code = %d", status);
    handle->cmdQueues[1] = clCreateCommandQueue(handle->contexts[1], handle->devices[1], 0, &status);
    CHECK_RESULT(handle->cmdQueues[1] == 0, "clCreateCommandQueue failed. Error code = %d", status);
#endif

    /* Create device buffers */
#ifdef USE_CL_EXTENSION
    handle->dstBuff = clCreateBuffer(handle->contexts[0], CL_MEM_BUS_ADDRESSABLE_AMD, transferSize, 0, &status);
    CHECK_RESULT((status != CL_SUCCESS), "clCreateBuffer failed. Error code = %d", status);
    status = clEnqueueMakeBuffersResidentAMD(handle->cmdQueues[0], 1, &handle->dstBuff, true, &dstBusAddress, 0, 0, 0);
    CHECK_RESULT((status != CL_SUCCESS), "clEnqueueMakeBuffersResidentAMD failed. Error code = %d", status);
    handle->extDstBuff = clCreateBuffer(handle->contexts[1], CL_MEM_EXTERNAL_PHYSICAL_AMD, transferSize, &dstBusAddress, &status);
    CHECK_RESULT((status != CL_SUCCESS), "clCreateBuffer failed. Error code = %d", status);
    handle->srcBuff = clCreateBuffer(handle->contexts[1], CL_MEM_READ_WRITE, transferSize, 0, &status);
    CHECK_RESULT(status != CL_SUCCESS, "clCreateBuffer failed. Error code = %d", status);
#else
    handle->dstBuff = clCreateBuffer(handle->contexts[0], CL_MEM_READ_WRITE, transferSize, 0, &status);
    CHECK_RESULT((status != CL_SUCCESS), "clCreateBuffer failed. Error code = %d", status);
    handle->srcBuff = clCreateBuffer(handle->contexts[1], CL_MEM_READ_WRITE, transferSize, 0, &status);
    CHECK_RESULT(status != CL_SUCCESS, "clCreateBuffer failed. Error code = %d", status);
#endif

    /* Create host side input and output buffers */
    handle->inputArr = (cl_uint*)malloc(transferSize);
    CHECK_RESULT(handle->inputArr == NULL, "Memory allocation failed: handle->inputArr");
    handle->outputArr = (cl_uint*)malloc(transferSize);
    CHECK_RESULT(handle->outputArr == NULL, "Memory allocation failed: handle->outputArr");
    for(unsigned int i = 0; i < (transferSize/sizeof(cl_uint)); ++i)
    {
        handle->inputArr[i] = i+1;
        handle->outputArr[i] = 0;
    }

    /* Enqueue write buffer */
    status = clEnqueueWriteBuffer(handle->cmdQueues[1], handle->srcBuff, CL_TRUE, 0, transferSize, handle->inputArr, 0, 0, NULL);
    CHECK_RESULT(status != CL_SUCCESS, "clEnqueueWriteBuffer failed. Error code = %d", status);

#ifdef USE_CL_EXTENSION
    /* Warm up */
    status = clEnqueueCopyBuffer(handle->cmdQueues[1], handle->srcBuff, handle->extDstBuff, 0, 0, handle->bufSize, 0, NULL, NULL);
    CHECK_RESULT(status != CL_SUCCESS, "clEnqueueCopyBuffer failed. Error code = %d", status);
    status = clFinish(handle->cmdQueues[1]);
    CHECK_RESULT(status != CL_SUCCESS, "clFinish failed. Error code = %d", status);
#endif

    return true;
}

/* DirectGMA functions - run */
bool oclDirectGMA_run(oclDirectGMA_context *handle, unsigned int numIterations)
{
    cl_int                             status = CL_SUCCESS;

    /* Events */
#ifdef USE_EVENTS_TO_PROFILE
    cl_event                           *eventList = (cl_event*)malloc(sizeof(cl_event)*numIterations);
    CHECK_RESULT(eventList == NULL, "Memory allocation failed: eventList");
#else
    timer                              dmaTimer;
    timerStart(&dmaTimer);
#endif

    /* Enqueue buffer transfer for numIterations */
    for (unsigned int i = 0; i < numIterations; i++)
    {
#ifdef USE_CL_EXTENSION
        /* clEnqueueCopyBuffer initiates the DMA transfer */
#ifdef USE_EVENTS_TO_PROFILE
        status = clEnqueueCopyBuffer(handle->cmdQueues[1], handle->srcBuff, handle->extDstBuff, 0, 0, handle->bufSize, 0, NULL, &eventList[i]);
#else
        status = clEnqueueCopyBuffer(handle->cmdQueues[1], handle->srcBuff, handle->extDstBuff, 0, 0, handle->bufSize, 0, NULL, NULL);
#endif
        CHECK_RESULT(status != CL_SUCCESS, "clEnqueueCopyBuffer failed. Error code = %d", status);
#else
#ifdef USE_MAP_UNMAP_BUFFERS
        /* Map the src buffer in READ mode */
        cl_uint *srcBuff = (cl_uint*)clEnqueueMapBuffer(handle->cmdQueues[1], handle->srcBuff, CL_TRUE, CL_MAP_READ, 0, handle->bufSize, 0, NULL, NULL, &status);
        CHECK_RESULT(status != CL_SUCCESS, "clEnqueueMapBuffer failed. Error code = %d", status);

        /* Map the src buffer in WRITE mode */
        cl_uint *dstBuff = (cl_uint*)clEnqueueMapBuffer(handle->cmdQueues[0], handle->dstBuff, CL_TRUE, CL_MAP_WRITE, 0, handle->bufSize, 0, NULL, NULL, &status);
        CHECK_RESULT(status != CL_SUCCESS, "clEnqueueMapBuffer failed. Error code = %d", status);

        /* Mem copy the buffer contents */
        memcpy(dstBuff, srcBuff, handle->bufSize);

        /* Unmap the src & dst buffers */
        status = clEnqueueUnmapMemObject(handle->cmdQueues[1], handle->srcBuff, srcBuff, 0, NULL, NULL);
        CHECK_RESULT(status != CL_SUCCESS, "clEnqueueUnmapMemObject failed. Error code = %d", status);
        status = clEnqueueUnmapMemObject(handle->cmdQueues[0], handle->dstBuff, dstBuff, 0, NULL, NULL);
        CHECK_RESULT(status != CL_SUCCESS, "clEnqueueUnmapMemObject failed. Error code = %d", status);
#else
        /* Read the src buffer into tmpBuff */
        cl_uint *tmpBuff = (cl_uint*)malloc(handle->bufSize);
        CHECK_RESULT(tmpBuff == NULL, "Memory allocation failed: tmpBuff");
        status = clEnqueueReadBuffer(handle->cmdQueues[1], handle->srcBuff, CL_TRUE, 0, handle->bufSize, tmpBuff, 0, 0, NULL);
        CHECK_RESULT(status != CL_SUCCESS, "clEnqueueReadBuffer failed. Error code = %d", status);

        /* Write the tmpBuff to the dst buffer */
        status = clEnqueueWriteBuffer(handle->cmdQueues[0], handle->dstBuff, CL_TRUE, 0, handle->bufSize, tmpBuff, 0, 0, NULL);
        CHECK_RESULT(status != CL_SUCCESS, "clEnqueueWriteBuffer failed. Error code = %d", status);
        free(tmpBuff);
#endif
#endif
    }
    status = clFinish(handle->cmdQueues[1]);
    CHECK_RESULT(status != CL_SUCCESS, "clFinish failed. Error code = %d", status);

    /* Compute elapsed time in transfer of data */
#ifdef USE_EVENTS_TO_PROFILE
    double sec = 0;
    for (unsigned int i = 0; i < numIterations; i++)
    {
        cl_ulong time_start, time_end;
        status = clGetEventProfilingInfo(eventList[i], CL_PROFILING_COMMAND_START, sizeof(time_start), &time_start, NULL);
        CHECK_RESULT(status != CL_SUCCESS, "clGetEventProfilingInfo failed. Error code = %d", status);
        status = clGetEventProfilingInfo(eventList[i], CL_PROFILING_COMMAND_END, sizeof(time_end), &time_end, NULL);
        CHECK_RESULT(status != CL_SUCCESS, "clGetEventProfilingInfo failed. Error code = %d", status);
        sec += (double)((time_end - time_start)/(1e+9));
        clReleaseEvent(eventList[i]);
    }
    free(eventList);
#else
    double sec = timerCurrent(&dmaTimer);
#endif

    /* Read back & verify DMA'ed data from second device */
    status = clEnqueueReadBuffer(handle->cmdQueues[0], handle->dstBuff, CL_TRUE, 0, handle->bufSize, handle->outputArr, 0, 0, NULL);
    CHECK_RESULT(status != CL_SUCCESS, "clEnqueueReadBuffer failed. Error code = %d", status);
    CHECK_RESULT((memcmp(handle->inputArr, handle->outputArr, handle->bufSize) != 0), "Memory copy failed");

    /* Read and print DMA transfer parameters */
    size_t param_size = 0;
    char *strSrcDeviceName = NULL;
    char *strDstDeviceName = NULL;
    status = clGetDeviceInfo(handle->devices[1], CL_DEVICE_NAME, 0, 0, &param_size);
    CHECK_RESULT(status != CL_SUCCESS, "clGetDeviceInfo failed. Error code = %d", status);
    strSrcDeviceName = (char*)malloc(param_size);
    CHECK_RESULT(strSrcDeviceName == NULL, "Memory allocation failed: strSrcDeviceName");
    status = clGetDeviceInfo(handle->devices[1], CL_DEVICE_NAME, param_size, strSrcDeviceName, 0);
    CHECK_RESULT(status != CL_SUCCESS, "clGetDeviceInfo failed. Error code = %d", status);
    status = clGetDeviceInfo(handle->devices[0], CL_DEVICE_NAME, 0, 0, &param_size);
    CHECK_RESULT(status != CL_SUCCESS, "clGetDeviceInfo failed. Error code = %d", status);
    strDstDeviceName = (char*)malloc(param_size);
    CHECK_RESULT(strDstDeviceName == NULL, "Memory allocation failed: strDstDeviceName");
    status = clGetDeviceInfo(handle->devices[0], CL_DEVICE_NAME, param_size, strDstDeviceName, 0);
    CHECK_RESULT(status != CL_SUCCESS, "clGetDeviceInfo failed. Error code = %d", status);
    printf("[%s -> %s]: ",strSrcDeviceName, strDstDeviceName);
    free(strSrcDeviceName);
    free(strDstDeviceName);

    /* Compute & print buffer copy bandwidth in GB/s */
    double perf = ((double)handle->bufSize*numIterations) / (sec*(double)(1024*1024*1024));
    printf("buffSize = %d KB, iterations = %d, DMA throughput = %.2f GB/s\n", (handle->bufSize/1024), numIterations, perf);

    return true;
}

/* DirectGMA functions - free */
void oclDirectGMA_free(oclDirectGMA_context *handle)
{
    cl_int                             status = CL_SUCCESS;

    if (handle->srcBuff)
    {
        status = clReleaseMemObject(handle->srcBuff);
        handle->srcBuff = NULL;
    }
    if (handle->dstBuff)
    {
        status = clReleaseMemObject(handle->dstBuff);
        handle->dstBuff = NULL;
    }
    if (handle->extDstBuff)
    {
        status = clReleaseMemObject(handle->extDstBuff);
        handle->extDstBuff = NULL;
    }
    if (handle->cmdQueues[0])
    {
        status = clReleaseCommandQueue(handle->cmdQueues[0]);
        handle->cmdQueues[0] = NULL;
    }
    if (handle->cmdQueues[1])
    {
        status = clReleaseCommandQueue(handle->cmdQueues[1]);
        handle->cmdQueues[1] = NULL;;
    }
    if (handle->contexts[0])
    {
        status = clReleaseContext(handle->contexts[0]);
        handle->contexts[0] = NULL;
    }
    if (handle->contexts[1])
    {
        status = clReleaseContext(handle->contexts[1]);
        handle->contexts[1] = NULL;;
    }
    if(handle->inputArr)
    {
        free(handle->inputArr);
        handle->inputArr = NULL;
    }
    if(handle->outputArr)
    {
        free(handle->outputArr);
        handle->outputArr = NULL;
    }
}

bool main()
{
    bool status = true;
    oclDirectGMA_context *handle = (oclDirectGMA_context*)malloc(sizeof(oclDirectGMA_context));
    CHECK_RESULT(handle == NULL, "Allocation of oclDirectGMA_context handle failed");

    unsigned int transferSizes[9] = {(192*2048*2*1), (256*2048*2*1), (192*4096*2*1), (256*4096*2*1), (192*2048*2*7), (256*2048*2*7), (192*4096*2*7), (256*4096*2*7), (64*1024*1024)};
    for (unsigned int i = 0; i < sizeof(transferSizes)/sizeof(unsigned int); i++)
    {
        status = oclDirectGMA_init(&handle, 0, transferSizes[i]);
        if (status) status = oclDirectGMA_run(handle, TRANSFER_COUNT);
        oclDirectGMA_free(handle);
    }
    for (unsigned int i = 0; i < sizeof(transferSizes)/sizeof(unsigned int); i++)
    {
        status = oclDirectGMA_init(&handle, 1, transferSizes[i]);
        if (status) status = oclDirectGMA_run(handle, TRANSFER_COUNT);
        oclDirectGMA_free(handle);
    }
    free(handle);
    return status;
}
