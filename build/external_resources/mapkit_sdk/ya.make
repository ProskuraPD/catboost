RESOURCES_LIBRARY()



IF (GCC OR CLANG)
    # headers must be fixed later
    CFLAGS(
        GLOBAL "-Wno-error=unused-parameter"
        GLOBAL "-Wno-error=sign-compare"
    )
ENDIF()

IF (OS_LINUX)
    # Qt + protobuf 2.6.1 + GL headers + GLES2
    DECLARE_EXTERNAL_RESOURCE(MAPKIT_SDK sbr:649684872)
    CFLAGS(
        GLOBAL "-I$MAPKIT_SDK_RESOURCE_GLOBAL/mapkit_sdk/include"
        GLOBAL "-I$MAPKIT_SDK_RESOURCE_GLOBAL/mapkit_sdk/local/linux.x86-64/include"
    )
    LDFLAGS_FIXED(
        "-L$MAPKIT_SDK_RESOURCE_GLOBAL/mapkit_sdk/local/linux.x86-64/lib"
        "-L$MAPKIT_SDK_RESOURCE_GLOBAL/mapkit_sdk/lib/x86_64-linux-gnu"
    )
ELSEIF (OS_ANDROID)
    # protobuf 2.6.1
    DECLARE_EXTERNAL_RESOURCE(MAPKIT_SDK sbr:549833385)
    CFLAGS(
        GLOBAL "-I$MAPKIT_SDK_RESOURCE_GLOBAL/mapkit_sdk/include"
    )
    IF (ARCH_ARM7)
        LDFLAGS_FIXED(
            "-L$MAPKIT_SDK_RESOURCE_GLOBAL/mapkit_sdk/local/android.armeabi-v7a/lib"
        )
    ELSEIF(ARCH_I386)
        CFLAGS(
            GLOBAL "-I$MAPKIT_SDK_RESOURCE_GLOBAL/mapkit_sdk/arch-x86/usr/include"
        )
        LDFLAGS_FIXED(
            "-L$MAPKIT_SDK_RESOURCE_GLOBAL/mapkit_sdk/local/android.x86/lib"
            "-L$MAPKIT_SDK_RESOURCE_GLOBAL/mapkit_sdk/arch-x86/usr/lib"
        )
    ELSE()
        MESSAGE(FATAL_ERROR Unsupported platform)
    ENDIF()
ELSEIF (OS_DARWIN)
    # Qt + protobuf 2.6.1
    DECLARE_EXTERNAL_RESOURCE(MAPKIT_SDK sbr:666723854)
    CFLAGS(
        GLOBAL "-I$MAPKIT_SDK_RESOURCE_GLOBAL/mapkit_sdk/include"
        GLOBAL "-I$MAPKIT_SDK_RESOURCE_GLOBAL/mapkit_sdk/local/darwin.x86-64/include"
    )
    LDFLAGS_FIXED(
        "-L$MAPKIT_SDK_RESOURCE_GLOBAL/mapkit_sdk/local/darwin.x86-64/lib"
    )
ELSEIF (OS_IOS)
    # protobuf 2.6.1
    IF (HOST_OS_LINUX)
        DECLARE_EXTERNAL_RESOURCE(MAPKIT_SDK sbr:666724415)
    ELSEIF (HOST_OS_DARWIN)
        DECLARE_EXTERNAL_RESOURCE(MAPKIT_SDK sbr:731932280)
    ELSE()
        MESSAGE(FATAL_ERROR Unsupported platform)
    ENDIF()
    CFLAGS(
        GLOBAL "-I$MAPKIT_SDK_RESOURCE_GLOBAL/mapkit_sdk/include"
    )
    IF (ARCH_ARM64)
        LDFLAGS_FIXED(
            "-L$MAPKIT_SDK_RESOURCE_GLOBAL/mapkit_sdk/local/ios.arm64/lib"
        )
    ELSEIF (ARCH_ARM7)
        LDFLAGS_FIXED(
            "-L$MAPKIT_SDK_RESOURCE_GLOBAL/mapkit_sdk/local/ios.armv7/lib"
        )
    ELSEIF (ARCH_I386)
        LDFLAGS_FIXED(
            "-L$MAPKIT_SDK_RESOURCE_GLOBAL/mapkit_sdk/local/ios.i386/lib"
        )
    ELSEIF (ARCH_X86_64)
        LDFLAGS_FIXED(
            "-L$MAPKIT_SDK_RESOURCE_GLOBAL/mapkit_sdk/local/ios.x86-64/lib"
        )
    ELSE()
        MESSAGE(FATAL_ERROR Unsupported platform)
    ENDIF()
ELSE()
    MESSAGE(FATAL_ERROR Unsupported platform)
ENDIF()

END()
