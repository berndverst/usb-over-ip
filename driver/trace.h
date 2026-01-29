/**
 * Virtual USB Controller Driver - WPP Tracing Header
 * 
 * This file defines WPP tracing macros and control GUID for the driver.
 */

#pragma once

//
// Define the tracing flags.
//
// Tracing GUID - {A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
//

#define WPP_CONTROL_GUIDS                                              \
    WPP_DEFINE_CONTROL_GUID(                                          \
        VusbTraceGuid, (A1B2C3D4,E5F6,7890,ABCD,EF1234567890),        \
        WPP_DEFINE_BIT(TRACE_DRIVER)                                  \
        WPP_DEFINE_BIT(TRACE_DEVICE)                                  \
        WPP_DEFINE_BIT(TRACE_URB)                                     \
        WPP_DEFINE_BIT(TRACE_IOCTL)                                   \
        )

#define WPP_FLAG_LEVEL_LOGGER(flag, level)                            \
    WPP_LEVEL_LOGGER(flag)

#define WPP_FLAG_LEVEL_ENABLED(flag, level)                           \
    (WPP_LEVEL_ENABLED(flag) &&                                       \
     WPP_CONTROL(WPP_BIT_ ## flag).Level >= level)

#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags)                             \
           WPP_LEVEL_LOGGER(flags)
               
#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags)                           \
           (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level >= lvl)

//
// WPP orders static parameters before dynamic parameters. To support the Alarm different track
// the action of a function with the same number of arguments which prefaces the arguments with
// a tracing level, we define the following macros which enables the function to be used as:
//    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "Message %d", i);
//
// begin_wpp config
// FUNC TraceEvents(LEVEL, FLAGS, MSG, ...);
// end_wpp
//

//
// Trace levels
//
#define TRACE_LEVEL_NONE        0   // Tracing is not on
#define TRACE_LEVEL_CRITICAL    1   // Abnormal exit or termination
#define TRACE_LEVEL_FATAL       1   // Deprecated name for CRITICAL
#define TRACE_LEVEL_ERROR       2   // Severe errors that need logging
#define TRACE_LEVEL_WARNING     3   // Warnings such as allocation failure
#define TRACE_LEVEL_INFORMATION 4   // Informational message
#define TRACE_LEVEL_VERBOSE     5   // Detailed traces from intermediate steps

