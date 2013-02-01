#import <Cocoa/Cocoa.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#include <OpenGL/gl.h>
#include "struct.h"
#include "util.h"
#include "variable.h"
#include "vm.h"

static NSWindow *window;

void hal_loop() {
    [NSApp run];
}

void xhal_window()
{
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    id menubar = [NSMenu new];
    id appMenuItem = [NSMenuItem new];
    [menubar addItem:appMenuItem];
    [NSApp setMainMenu:menubar];
    id appMenu = [NSMenu new];
    id appName = [[NSProcessInfo processInfo] processName];
    id quitTitle = [@"Quit " stringByAppendingString:appName];
    id quitMenuItem = [[NSMenuItem alloc] initWithTitle:quitTitle
                                                 action:@selector(terminate:)
                                          keyEquivalent:@"q"];
    [appMenu addItem:quitMenuItem];
    [appMenuItem setSubmenu:appMenu];
    window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 200, 200)
                                         styleMask:NSTitledWindowMask |
              NSClosableWindowMask |
              NSMiniaturizableWindowMask |
              NSResizableWindowMask
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    [window cascadeTopLeftFromPoint:NSMakePoint(20,20)];
    [window setTitle:appName];
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

@interface xGLView : NSOpenGLView

@end

@implementation xGLView

- (id)initWithFrame:(NSRect)frameRect
{
    NSOpenGLPixelFormatAttribute attr[] =
	{
        NSOpenGLPFADoubleBuffer,
		NSOpenGLPFAAccelerated,
		NSOpenGLPFAColorSize, (NSOpenGLPixelFormatAttribute) 32,
		NSOpenGLPFADepthSize, (NSOpenGLPixelFormatAttribute) 23,
		(NSOpenGLPixelFormatAttribute) 0
	};
	NSOpenGLPixelFormat *nsglFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:attr];
	
    if (self = [super initWithFrame:frameRect pixelFormat:nsglFormat]) {}
	return self;
}

- (void)prepareOpenGL
{
    glMatrixMode(GL_MODELVIEW);
	glClearColor(0, 0, .25, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
	
    glShadeModel(GL_SMOOTH);
	glClearDepth(1.0f);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
}

- (float)get_float:(const struct variable *)point at:(uint32_t)i
{
    const struct variable *f = (const struct variable*)array_get(point->list, i);
    return f->floater;
}

- (void)drawRect:(NSRect)rect
{
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
    glColor3f(1.0f, 0.85f, 0.35f);
    glBegin(GL_TRIANGLES);
    {
        glVertex3f(  0.0,  0.6, 0.0);
        glVertex3f( -0.2, -0.3, 0.0);
        glVertex3f(  0.2, -0.3 ,0.0);
    }
    glEnd();
    [[self openGLContext] flushBuffer];
}


@end

@interface GLView : NSOpenGLView {
    const struct variable *shape;
}
@end

@implementation GLView

- (id)initWithFrame:(NSRect)frameRect
{
    NSOpenGLPixelFormatAttribute attr[] =
	{
        NSOpenGLPFADoubleBuffer,
		NSOpenGLPFAAccelerated,
		NSOpenGLPFAColorSize, (NSOpenGLPixelFormatAttribute) 32,
		NSOpenGLPFADepthSize, (NSOpenGLPixelFormatAttribute) 23,
		(NSOpenGLPixelFormatAttribute) 0
	};
	NSOpenGLPixelFormat *nsglFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:attr];
	
    if (self = [super initWithFrame:frameRect pixelFormat:nsglFormat]) {}
	return self;
}

- (void)setShape:(const struct variable *)ape {
    self->shape = ape;
}

- (void)prepareOpenGL
{
    glMatrixMode(GL_MODELVIEW);
	glClearColor(0, 0, .25, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
	
    glShadeModel(GL_SMOOTH);
	glClearDepth(1.0f);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
}

- (float)get_float:(const struct variable *)point at:(uint32_t)i
{
    const struct variable *f = (const struct variable*)array_get(point->list, i);
    return f->floater;
}

- (void)drawRect:(NSRect)rect
{
    //    assert_message(shape->type == VAR_LST, "shape not list");
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
    glColor3f(1.0f, 0.85f, 0.35f);
    glBegin(GL_TRIANGLES);
    {
        if (!self->shape) {
            glVertex3f(  0.0,  0.6, 0.0);
            glVertex3f( -0.2, -0.3, 0.0);
            glVertex3f(  0.2, -0.3 ,0.0);
        } else {
            for (uint32_t i=0; i<self->shape->list->length; i++) {
                const struct variable *point = (const struct variable*)array_get(self->shape->list, i);
                assert_message(point->type == VAR_LST, "point not list");
                float x = [self get_float:point at:0];
                float y = [self get_float:point at:1];
                float z = [self get_float:point at:2];
                glVertex3f( x, y, z);
            }
        }
    }
    glEnd();
    [[self openGLContext] flushBuffer];
}

@end

void hal_graphics(const struct variable *shape)
{
    NSView *content = [window contentView];
    NSRect rect = [content frame];
    GLView *graph = [[GLView alloc] initWithFrame:rect];
    [graph setShape:shape];
    [graph drawRect:rect];
    [content addSubview:graph];
}

void add_graphics()
{
    NSView *content = [window contentView];
    NSRect rect = [content frame];
    //rect.size.width /= 2;
    GLView *graph = [[GLView alloc] initWithFrame:rect];
    [graph drawRect:rect];
    [content addSubview:graph];
}


void hal_image()
{
    NSView *content = [window contentView];
    NSRect rect = [content frame];
    rect.origin.x = rect.size.width/2;
    NSImageView *iv = [[NSImageView alloc] initWithFrame:rect];
    
    NSURL *url = [NSURL URLWithString:@"http://www.cgl.uwaterloo.ca/~csk/projects/starpatterns/noneuclidean/323ball.jpg"];
    NSImage *pic = [[NSImage alloc] initWithContentsOfURL:url];
    if (pic)
        [iv setImage:pic];
    [content addSubview:iv];
}

void hal_sound(const char *address)
{
    //NSURL *url = [NSURL URLWithString:@"http://www.wavlist.com/soundfx/011/duck-quack3.wav"];
    NSString *str = [NSString stringWithCString:address encoding:NSUTF8StringEncoding];
    NSURL *url = [NSURL URLWithString:str];
    NSSound *sound = [[NSSound alloc] initWithContentsOfURL:url byReference:FALSE];
    [sound play];
}

struct Sound {
    short *samples;
	size_t buf_size;
    unsigned sample_rate;
    int seconds;
};

#define CASE_RETURN(err) case (err): return "##err"
const char* al_err_str(ALenum err) {
    switch(err) {
            CASE_RETURN(AL_NO_ERROR);
            CASE_RETURN(AL_INVALID_NAME);
            CASE_RETURN(AL_INVALID_ENUM);
            CASE_RETURN(AL_INVALID_VALUE);
            CASE_RETURN(AL_INVALID_OPERATION);
            CASE_RETURN(AL_OUT_OF_MEMORY);
    }
    return "unknown";
}
#undef CASE_RETURN

#define __al_check_error(file,line) \
do { \
ALenum err = alGetError(); \
for(; err!=AL_NO_ERROR; err=alGetError()) { \
printf("AL Error %s at %s:%d\n", al_err_str(err), file, line ); \
} \
}while(0)

#define al_check_error() \
__al_check_error(__FILE__, __LINE__)

/* initialize OpenAL */
ALuint init_al() {
	ALCdevice *dev = NULL;
	ALCcontext *ctx = NULL;
    
	const char *defname = alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
    printf("Default ouput device: %s\n", defname);
    
	dev = alcOpenDevice(defname);
	ctx = alcCreateContext(dev, NULL);
	alcMakeContextCurrent(ctx);
    
	/* Create buffer to store samples */
	ALuint buf;
	alGenBuffers(1, &buf);
	al_check_error();
    return buf;
}

/* Dealloc OpenAL */
void exit_al() {
	ALCdevice *dev = NULL;
	ALCcontext *ctx = NULL;
	ctx = alcGetCurrentContext();
	dev = alcGetContextsDevice(ctx);
    
	alcMakeContextCurrent(NULL);
	alcDestroyContext(ctx);
	alcCloseDevice(dev);
}

void hal_sleep(uint32_t miliseconds)
{
    struct timespec req={0};
    time_t sec = (int)(miliseconds/1000);
    miliseconds = miliseconds - (sec * 1000);
    req.tv_sec = sec;
    req.tv_nsec = miliseconds * 1000000L;
    while (nanosleep(&req,&req) == -1)
        continue;
}

#define SYNTH_SAMPLE_RATE 44100 // CD quality

void hal_synth(const uint8_t *bytes, uint32_t length)
{
    short *samples = (short*)bytes;
    uint32_t size = length / sizeof(short);
    float duration = size * 1.0f / SYNTH_SAMPLE_RATE;
    
    ALuint buf = init_al();
    /* Download buffer to OpenAL */
	alBufferData(buf, AL_FORMAT_MONO16, samples, size, SYNTH_SAMPLE_RATE);
	al_check_error();
    
	/* Set-up sound source and play buffer */
	ALuint src = 0;
	alGenSources(1, &src);
	alSourcei(src, AL_BUFFER, buf);
	alSourcePlay(src);
    
	/* While sound is playing, sleep */
	al_check_error();
    
    hal_sleep(duration*1000);
    
	exit_al();
}

#define QUEUEBUFFERCOUNT 2
#define QUEUEBUFFERSIZE 9999

ALvoid hal_audio_loop(ALvoid)
{
    ALCdevice   *pCaptureDevice;
    const       ALCchar *szDefaultCaptureDevice;
    ALint       lSamplesAvailable;
    ALchar      Buffer[QUEUEBUFFERSIZE];
    ALuint      SourceID, TempBufferID;
    ALuint      BufferID[QUEUEBUFFERCOUNT];
    ALuint      ulBuffersAvailable = QUEUEBUFFERCOUNT;
    ALuint      ulUnqueueCount, ulQueueCount;
    ALint       lLoop, lFormat, lFrequency, lBlockAlignment, lProcessed, lPlaying;
    ALboolean   bPlaying = AL_FALSE;
    ALboolean   bPlay = AL_FALSE;
    
    // does not setup the Wave Device's Audio Mixer to select a recording input or recording level.
    
	ALCdevice *dev = NULL;
	ALCcontext *ctx = NULL;
    
	const char *defname = alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
    printf("Default ouput device: %s\n", defname);
    
	dev = alcOpenDevice(defname);
	ctx = alcCreateContext(dev, NULL);
	alcMakeContextCurrent(ctx);
    
    // Generate a Source and QUEUEBUFFERCOUNT Buffers for Queuing
    alGetError();
    alGenSources(1, &SourceID);
    
    for (lLoop = 0; lLoop < QUEUEBUFFERCOUNT; lLoop++)
        alGenBuffers(1, &BufferID[lLoop]);
    
    if (alGetError() != AL_NO_ERROR) {
        printf("Failed to generate Source and / or Buffers\n");
        return;
    }
    
    ulUnqueueCount = 0;
    ulQueueCount = 0;
    
    // Get list of available Capture Devices
    const ALchar *pDeviceList = alcGetString(NULL, ALC_CAPTURE_DEVICE_SPECIFIER);
    if (pDeviceList) {
        printf("Available Capture Devices are:-\n");
        
        while (*pDeviceList) {
            printf("%s\n", pDeviceList);
            pDeviceList += strlen(pDeviceList) + 1;
        }
    }
    
    szDefaultCaptureDevice = alcGetString(NULL, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER);
    printf("\nDefault Capture Device is '%s'\n\n", szDefaultCaptureDevice);
    
    // The next call can fail if the WaveDevice does not support the requested format, so the application
    // should be prepared to try different formats in case of failure
    
    lFormat = AL_FORMAT_MONO16;
    lFrequency = 44100;
    lBlockAlignment = 2;
    
    long lTotalProcessed = 0;
    long lOldSamplesAvailable = 0;
    long lOldTotalProcessed = 0;
    
    pCaptureDevice = alcCaptureOpenDevice(szDefaultCaptureDevice, lFrequency, lFormat, lFrequency);
    if (pCaptureDevice) {
        printf("Opened '%s' Capture Device\n\n", alcGetString(pCaptureDevice, ALC_CAPTURE_DEVICE_SPECIFIER));
        
        printf("start capture\n");
        alcCaptureStart(pCaptureDevice);
        bPlay = AL_TRUE;
        
        for (;;) {
            //alcCaptureStop(pCaptureDevice);
            
            alGetError();
            alcGetIntegerv(pCaptureDevice, ALC_CAPTURE_SAMPLES, 1, &lSamplesAvailable);
            
            if ((lOldSamplesAvailable != lSamplesAvailable) || (lOldTotalProcessed != lTotalProcessed)) {
                printf("Samples available is %d, Buffers Processed %ld\n", lSamplesAvailable, lTotalProcessed);
                lOldSamplesAvailable = lSamplesAvailable;
                lOldTotalProcessed = lTotalProcessed;
            }
            
            // If the Source is (or should be) playing, get number of buffers processed
            // and check play status
            if (bPlaying) {
                alGetSourcei(SourceID, AL_BUFFERS_PROCESSED, &lProcessed);
                while (lProcessed) {
                    lTotalProcessed++;
                    
                    // Unqueue the buffer
                    alSourceUnqueueBuffers(SourceID, 1, &TempBufferID);
                    
                    // Update unqueue count
                    if (++ulUnqueueCount == QUEUEBUFFERCOUNT)
                        ulUnqueueCount = 0;
                    
                    // Increment buffers available
                    ulBuffersAvailable++;
                    
                    lProcessed--;
                }
                
                // If the Source has stopped (been starved of data) it will need to be
                // restarted
                alGetSourcei(SourceID, AL_SOURCE_STATE, &lPlaying);
                if (lPlaying == AL_STOPPED) {
                    printf("Buffer Stopped, Buffers Available is %d\n", ulBuffersAvailable);
                    bPlay = AL_TRUE;
                }
            }
            
            if ((lSamplesAvailable > (QUEUEBUFFERSIZE / lBlockAlignment)) && !(ulBuffersAvailable)) {
                printf("underrun!\n");
            }
            
            // When we have enough data to fill our QUEUEBUFFERSIZE byte buffer, grab the samples
            else if ((lSamplesAvailable > (QUEUEBUFFERSIZE / lBlockAlignment)) && (ulBuffersAvailable)) {
                // Consume Samples
                alcCaptureSamples(pCaptureDevice, Buffer, QUEUEBUFFERSIZE / lBlockAlignment);
                alBufferData(BufferID[ulQueueCount], lFormat, Buffer, QUEUEBUFFERSIZE, lFrequency);
                
                // Queue the buffer, and mark buffer as queued
                alSourceQueueBuffers(SourceID, 1, &BufferID[ulQueueCount]);
                if (++ulQueueCount == QUEUEBUFFERCOUNT)
                    ulQueueCount = 0;
                
                // Decrement buffers available
                ulBuffersAvailable--;
                
                // If we need to start the Source do it now IF AND ONLY IF we have queued at least 2 buffers
                if ((bPlay) && (ulBuffersAvailable <= (QUEUEBUFFERCOUNT - 2))) {
                    alSourcePlay(SourceID);
                    printf("Buffer Starting \n");
                    bPlaying = AL_TRUE;
                    bPlay = AL_FALSE;
                }
            }
        }
        alcCaptureCloseDevice(pCaptureDevice);
    } else
        printf("WaveDevice is unavailable, or does not supported the request format\n");
    
    alSourceStop(SourceID);
    alDeleteSources(1, &SourceID);
    for (lLoop = 0; lLoop < QUEUEBUFFERCOUNT; lLoop++)
        alDeleteBuffers(1, &BufferID[lLoop]);
}

void hal_label(int x, int y, int w, int h, const char *str) {
    NSView *content = [window contentView];
    NSRect rect = NSMakeRect(x, y, w, h);
    NSTextField *textField;
    textField = [[NSTextField alloc] initWithFrame:rect];
    NSString *string = [NSString stringWithUTF8String:str];
    [textField setStringValue:string];
    [textField setBezeled:NO];
    [textField setDrawsBackground:NO];
    [textField setEditable:NO];
    [textField setSelectable:NO];
    [content addSubview:textField];
}

void hal_button(int x, int y, int w, int h, const char *str, const char *img1, const char* img2) {
    NSView *content = [window contentView];
    int frameHeight = [content frame].size.height;
    NSRect rect = NSMakeRect(x, frameHeight - y - h, w, h);
    
    NSButton *my = [[NSButton alloc] initWithFrame:rect];
    [content addSubview: my];
    NSString *string = [NSString stringWithUTF8String:str];
    [my setTitle:string];
    
    if (img1) {
        string = [NSString stringWithUTF8String:img1];
        NSURL* url = [NSURL fileURLWithPath:string];
        NSImage *image = [[NSImage alloc] initWithContentsOfURL: url];
        [my setImage:image] ;
    }
    
    //    [my setTarget:self];
    [my setAction:@selector(invisible)];
    [my setButtonType:NSMomentaryLightButton];
    [my setBezelStyle:NSTexturedSquareBezelStyle];
}

void hal_input(int x, int y, int w, int h, const char *str, BOOL multiline) {
    NSView *content = [window contentView];
    int frameHeight = [content frame].size.height;
    NSRect rect = NSMakeRect(x, frameHeight - y - h, w, h);
    NSString *string = [NSString stringWithUTF8String:str];
    
    NSView *textField;
    if (multiline)
        textField = [[NSTextView alloc] initWithFrame:rect];
    else
        textField = [[NSTextField alloc] initWithFrame:rect];
    [textField insertText:string];
    
    [content addSubview:textField];
}

@interface HALarray : NSObject <NSTableViewDataSource, NSTableViewDelegate> {
    struct context *context;
    const struct variable *data;
    const struct variable *logic;
}

@end

@implementation HALarray

+ (HALarray *)arrayWithData:(const struct variable *)list
                      logic:(const struct variable *)logic
                  inContext:(struct context *)cxt
{
    HALarray *ha = [HALarray alloc];
    ha->data = list;
    ha->logic = logic;
    ha->context = cxt;
    return ha;
}

- (id)          tableView:(NSTableView *) aTableView
objectValueForTableColumn:(NSTableColumn *) aTableColumn
                      row:(long) rowIndex {
    struct variable *item = array_get(self->data->list, rowIndex);
    const char *name = variable_value_str(self->context, item);
    return [NSString stringWithUTF8String:name];
}

- (long)numberOfRowsInTableView:(NSTableView *)aTableView {
    return self->data->list->length;
}

- (void) tableViewSelectionDidChange:(NSNotification *)notification
{
    NSTableView* table = [notification object];
    int row = [table selectedRow];
    if (row == -1)
        return;
    if (self->logic->type != VAR_NIL)
        execute(self->logic->str, self->context->find);
}
@end

void hal_table(struct context *context, int x, int y, int w, int h,
               struct variable *list, struct variable *logic) {
    NSView *content = [window contentView];
    NSRect rect = NSMakeRect(x, y, w, h);
    NSScrollView * tableContainer = [[NSScrollView alloc] initWithFrame:rect];
    w -= 16;
    rect = NSMakeRect(x, y, w, h);
    NSTableView *tableView = [[NSTableView alloc] initWithFrame:rect];
    NSTableColumn * column1 = [[NSTableColumn alloc] initWithIdentifier:@"Col1"];
    [[column1 headerCell] setStringValue:@"yo"];
    
    [tableView addTableColumn:column1];
    //    NSArray *source = [NSArray arrayWithObjects:@"3",@"1",@"4",nil];
    HALarray *source = [HALarray arrayWithData:list logic:logic inContext:context];
    [tableView setDelegate:source];
    [tableView setDataSource:(id<NSTableViewDataSource>)source];
    //  [tableView reloadData];
    [tableContainer setDocumentView:tableView];
    [tableContainer setHasVerticalScroller:YES];
    [content addSubview:tableContainer];
}

void hal_window()
{
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    id menubar = [NSMenu new];
    id appMenuItem = [NSMenuItem new];
    [menubar addItem:appMenuItem];
    [NSApp setMainMenu:menubar];
    id appMenu = [NSMenu new];
    id appName = [[NSProcessInfo processInfo] processName];
    id quitTitle = [@"Quit " stringByAppendingString:appName];
    id quitMenuItem = [[NSMenuItem alloc] initWithTitle:quitTitle
                                                 action:@selector(terminate:)
                                          keyEquivalent:@"q"];
    [appMenu addItem:quitMenuItem];
    [appMenuItem setSubmenu:appMenu];
    window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 200, 200)
                                         styleMask:NSTitledWindowMask |
              NSClosableWindowMask |
              NSMiniaturizableWindowMask |
              NSResizableWindowMask
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    [window cascadeTopLeftFromPoint:NSMakePoint(20,20)];
    [window setTitle:appName];
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

int xmain(int argc, char *argv[])
{
    hal_window();
    hal_graphics(NULL);
    [NSApp run];
    return 0;
}
