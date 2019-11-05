//
//  NkeClient
//  main.cpp - Main for connecting to and logging event notifications from NKE filter
//
//  Copyright (c) 2016 Slava Imameev. All rights reserved.
//

#include "NkeConnection.h"

#include <iostream>
#include <sys/ioctl.h>
#include <stdio.h>
#include <termios.h>
#include <fcntl.h>

//-------------------------------------------------------------

// ioctl command definitions
#define NKE_START_DIVERTING _IO('Z',1)
#define NKE_STOP_DIVERTING _IO('Z',2)

//-------------------------------------------------------------

io_connect_t    connection;

//-------------------------------------------------------------

void NkeSocketHandler(io_connect_t connection); // Previously IOReturn
const char* NkeEventToString(NkeSocketFilterEvent   event);
void NkeInitTermios(void);
void NkeResetTermios(void);
static struct termios oldSettings, newSettings;

//-------------------------------------------------------------

int main(int argc, const char * argv[])
{
    
    kern_return_t   kr;
    pthread_t       nkeSocketThread;
    int error;
    
    // Connect to NKE filter driver
    kr = NkeOpenDlDriver( &connection );
    if (KERN_SUCCESS != kr) {
        return (-1);
    }
    
    // Open device node for sending ioctls
    int fd = open("/dev/archon", O_RDWR);
    if (fd < 0) {
        printf("error opening archon device\n");
        return(-1);
    }
    
    // Send ioctl to start filtering
    error = ioctl(fd, NKE_START_DIVERTING, NULL);
    if (error < 0) {
        printf("error starting packet diversion\n");
        printf("ioctl failed and returned errno %s\n",strerror(errno));
    }
    close(fd);
    
    // Create thread for handling socket notifications
    error = pthread_create(&nkeSocketThread, (pthread_attr_t *)0,
                            (void* (*)(void*))NkeSocketHandler, (void *)connection);

    if (error) {
        perror("pthread_create( SocketNotificationHandler )");
        nkeSocketThread = NULL;
    }

    if (nkeSocketThread) {
        pthread_join(nkeSocketThread, (void **)&kr);
    }

    printf("Back in main thread!\n");
    
    // Open device node for sending ioctls
    fd = open("/dev/archon", O_RDWR);
    if (fd < 0) {
        printf("error opening archon device\n");
    }

    // Send ioctl to stop filtering
    error = ioctl(fd, NKE_STOP_DIVERTING, NULL);
    if (error < 0) {
        printf("error stopping packet diversion\n");
        printf("ioctl failed and returned errno %s\n",strerror(errno));
    }
    close(fd);
    
    IOServiceClose(connection);
    
    return 0;
}

//-------------------------------------------------------------

void NkeInitTermios(void) //struct termios &oldSettings, struct termios &newSettings)
{
    fcntl(0, F_SETFL, O_NONBLOCK);
    tcgetattr(0, &oldSettings); /* grab old terminal i/o settings */
    newSettings = oldSettings; /* make new settings same as old settings */
    newSettings.c_lflag &= ~ICANON; /* disable buffered i/o */
    newSettings.c_lflag &= ~ECHO; /* disable echo */
    tcsetattr(0, TCSANOW, &newSettings); /* use these new terminal i/o settings now */
}

void NkeResetTermios(void)
{
    tcsetattr(0, TCSANOW, &oldSettings);
}

//-------------------------------------------------------------

void NkeSocketHandler(io_connect_t connection)
{
    kern_return_t       kr; // Used throughout function
    uint32_t            dataSize;
    IODataQueueMemory  *queueMappedMemory;
    vm_size_t           queueMappedMemorySize;
    mach_vm_address_t   address = NULL;
    mach_vm_size_t      size = 0x0;
    mach_port_t         recvPort; // Port for receiving filter notifications
//    mach_vm_address_t   sharedBuffers[ kt_NkeSocketBuffersNumber ];
//    mach_vm_size_t      sharedBuffersSize[ kt_NkeSocketBuffersNumber ];
    
    // Allocate a Mach port to receive notifications from the IODataQueue
    if( !( recvPort = IODataQueueAllocateNotificationPort() ) ){
        printf("failed to allocate notification port\n");
        kr = kIOReturnError;
        goto __exit;
    }
    
//    // Initialize shared buffers
//    for( int i = 0; i < kt_NkeSocketBuffersNumber; ++i ){
//        sharedBuffers[ i ] = NULL;
//        sharedBuffersSize[ i ] = 0;
//    }
//
//    // Map kernel buffers into process address space
//    for( int i = 0; i < kt_NkeSocketBuffersNumber; ++i ){
//
//        // Will call clientMemoryForType() inside our user client class
//        kr = IOConnectMapMemory( connection,
//                                 kt_NkeAclTypeSocketDataBase + i,
//                                 mach_task_self(),
//                                 &sharedBuffers[ i ],
//                                 &sharedBuffersSize[ i ],
//                                 kIOMapAnywhere );
//
//        if( kr != kIOReturnSuccess ){
//            printf("failed to map memory (%d)\n",kr);
//            goto __exit;
//        }
//    }

    // Will call registerNotificationPort() inside our user client class
    kr = IOConnectSetNotificationPort(connection, kt_NkeNotifyTypeSocketFilter, recvPort, 0);
    if( kr != kIOReturnSuccess ){
        printf("failed to register notification port (%d)\n", kr);
        goto __exit;
    }
    
    // Map a buffer used to deliver events from the filter, a data queue is implemented over it,
    // Will call clientMemoryForType() inside our user client class
    kr = IOConnectMapMemory( connection,
                            kt_NkeNotifyTypeSocketFilter,
                            mach_task_self(),
                            &address,
                            &size,
                            kIOMapAnywhere );
    if( kr != kIOReturnSuccess ){
        printf("failed to map memory (%d)\n",kr);
        goto __exit;
    }
    
    queueMappedMemory = (IODataQueueMemory *)address;
    queueMappedMemorySize = size;
    
    // Set up exit condition
    bool quit;
    quit = false;
    NkeInitTermios();
    printf("Press 'q' to handle remaining notifications and exit...\n");
    
    // While loop for filter notifications (queueMappedMemory and recvPort must be non-NULL)
    while( kIOReturnSuccess == IODataQueueWaitForAvailableData(queueMappedMemory, recvPort) && !quit ) {
        
        // Check for 'q' on stdin
        char command = getchar();
        printf("command is: %c\n", command);
        if (command == 'q') {
            quit = true;
        }
        
        // While loop for handling available filter notifications
        while( IODataQueueDataAvailable(queueMappedMemory) ){
            
            NkeSocketFilterNotification notification;
            dataSize = sizeof(notification);
            
            // Extract event descriptor from data queue
            kr = IODataQueueDequeue(queueMappedMemory, &notification, &dataSize);
            if( kr == kIOReturnSuccess ){
                time_t current = time(NULL);
                
                printf("NKE event: %s", NkeEventToString( notification.event ) );
                printf("\t%s\n", ctime(&current));
                
                if( notification.event == NkeSocketFilterEventDataIn || notification.event == NkeSocketFilterEventDataOut ){
                    
                    // Create a response to the filter
                    NkeSocketFilterServiceResponse response;
                    bzero(&response, sizeof( response ));
                    
                    memcpy( response.buffersToRelease, notification.eventData.inputoutput.buffers, sizeof( response.buffersToRelease ) );
                    response.property[ 0 ].type = NkeSocketDataPropertyTypePermission;
                    response.property[ 0 ].socketId = notification.socketId;
                    response.property[ 0 ].dataIndex = notification.eventData.inputoutput.dataIndex;
                    response.property[ 0 ].value.permission.allowData = 0x1;
                    
                    response.property[ 1 ].type = NkeSocketDataPropertyTypeUnknown; // a terminating entry
                    
                    // Send to the driver (the driver will inject data synchronously)
                    size_t notUsed = sizeof(response);
                    
                    kr = IOConnectCallStructMethod( connection,
                                                   kt_NkeUserClientSocketFilterResponse,
                                                   (const void*)&response,
                                                   sizeof(response),
                                                   &response,
                                                   &notUsed );
                    if( kIOReturnSuccess != kr ){
                        printf("IOConnectCallStructMethod failed with kr = 0x%X\n", kr);
                    }
                }

            } else {
                printf("IODataQueueDequeue failed with kr = 0x%X\\n", kr);
            }
            
        }
        
    }
    
__exit:
    
    // Reset termios to previous configuration
    NkeResetTermios();
    
//    // Unmap memory buffers on exit
//    for( int i = 0; i < kt_NkeSocketBuffersNumber; ++i ){
//
//        if( !sharedBuffers[ i ] )
//            continue;
//
//        kr = IOConnectUnmapMemory( connection,
//                                   kt_NkeAclTypeSocketDataBase + i,
//                                   mach_task_self(),
//                                   sharedBuffers[ i ] );
//        if( kr != kIOReturnSuccess ){
//            printf("failed to unmap memory (%d)\n", kr);
//        }
//    }
    
    if( address ){
        kr = IOConnectUnmapMemory( connection,
                                  kt_NkeNotifyTypeSocketFilter,
                                  mach_task_self(),
                                  address );
        if( kr != kIOReturnSuccess ){
            printf("failed to unmap memory (%d)\n", kr);
        }
    }
    
    // Destroy notification port
    if( recvPort ) {
        mach_port_destroy(mach_task_self(), recvPort);
    }
    
    // Exit the pthread
    pthread_exit(NULL);
    
}

//-------------------------------------------------------------

const char* NkeEventToString( NkeSocketFilterEvent event )
{
    switch( event )
    {
        case NkeSocketFilterEventUnknown: return "NkeSocketFilterEventUnknown";
        case NkeSocketFilterEventConnected: return "NkeSocketFilterEventConnected";
        case NkeSocketFilterEventDisconnected: return "NkeSocketFilterEventDisconnected";
        case NkeSocketFilterEventShutdown: return "NkeSocketFilterEventShutdown";
        case NkeSocketFilterEventCantrecvmore: return "NkeSocketFilterEventCantrecvmore";
        case NkeSocketFilterEventCantsendmore: return "NkeSocketFilterEventCantsendmore";
        case NkeSocketFilterEventClosing: return "NkeSocketFilterEventClosing";
        case NkeSocketFilterEventBound: return "NkeSocketFilterEventBound";
        case NkeSocketFilterEventDataIn: return "NkeSocketFilterEventDataIn";
        case NkeSocketFilterEventDataOut: return "NkeSocketFilterEventDataOut";
        default: return "UNKNOWN";
    }
}

//-------------------------------------------------------------

