#!/usr/bin/env python3
"""Create a standalone native UI-test runner; the game app is installed separately."""
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE / "generated/MobileControlsUITests.xcodeproj"
PROJECT.mkdir(parents=True, exist_ok=True)
source = str(HERE / "MobileControlsUITests.m")
pbx = r'''// !$*UTF8*$!
{
 archiveVersion = 1; classes = {}; objectVersion = 56;
 objects = {
 A00000000000000000000001 = {isa = PBXProject; attributes = {LastUpgradeCheck = 2620;}; buildConfigurationList = A00000000000000000000002; compatibilityVersion = "Xcode 14.0"; developmentRegion = en; hasScannedForEncodings = 0; knownRegions = (en,Base); mainGroup = A00000000000000000000003; productRefGroup = A00000000000000000000004; projectDirPath = ""; projectRoot = ""; targets = (A00000000000000000000005);};
 A00000000000000000000002 = {isa = XCConfigurationList; buildConfigurations = (A00000000000000000000006,A00000000000000000000007); defaultConfigurationIsVisible = 0; defaultConfigurationName = Debug;};
 A00000000000000000000003 = {isa = PBXGroup; children = (A00000000000000000000008,A00000000000000000000004); sourceTree = "<group>";};
 A00000000000000000000004 = {isa = PBXGroup; children = (A00000000000000000000009); name = Products; sourceTree = "<group>";};
 A00000000000000000000005 = {isa = PBXNativeTarget; buildConfigurationList = A0000000000000000000000A; buildPhases = (A0000000000000000000000B,A0000000000000000000000C,A0000000000000000000000D); buildRules = (); dependencies = (); name = MobileControlsUITests; productName = MobileControlsUITests; productReference = A00000000000000000000009; productType = "com.apple.product-type.bundle.ui-testing";};
 A00000000000000000000006 = {isa = XCBuildConfiguration; buildSettings = {CLANG_ENABLE_MODULES = YES; SDKROOT = iphoneos; IPHONEOS_DEPLOYMENT_TARGET = 17.0;}; name = Debug;};
 A00000000000000000000007 = {isa = XCBuildConfiguration; buildSettings = {CLANG_ENABLE_MODULES = YES; SDKROOT = iphoneos; IPHONEOS_DEPLOYMENT_TARGET = 17.0;}; name = Release;};
 A00000000000000000000008 = {isa = PBXFileReference; lastKnownFileType = sourcecode.c.objc; name = MobileControlsUITests.m; path = "SOURCE_PATH"; sourceTree = "<absolute>";};
 A00000000000000000000009 = {isa = PBXFileReference; explicitFileType = wrapper.cfbundle; includeInIndex = 0; path = MobileControlsUITests.xctest; sourceTree = BUILT_PRODUCTS_DIR;};
 A0000000000000000000000A = {isa = XCConfigurationList; buildConfigurations = (A0000000000000000000000E,A0000000000000000000000F); defaultConfigurationIsVisible = 0; defaultConfigurationName = Debug;};
 A0000000000000000000000B = {isa = PBXSourcesBuildPhase; buildActionMask = 2147483647; files = (A00000000000000000000010); runOnlyForDeploymentPostprocessing = 0;};
 A0000000000000000000000C = {isa = PBXFrameworksBuildPhase; buildActionMask = 2147483647; files = (); runOnlyForDeploymentPostprocessing = 0;};
 A0000000000000000000000D = {isa = PBXResourcesBuildPhase; buildActionMask = 2147483647; files = (); runOnlyForDeploymentPostprocessing = 0;};
 A0000000000000000000000E = {isa = XCBuildConfiguration; buildSettings = {CLANG_ENABLE_MODULES = YES; CODE_SIGNING_ALLOWED = NO; CODE_SIGN_STYLE = Automatic; GENERATE_INFOPLIST_FILE = YES; IPHONEOS_DEPLOYMENT_TARGET = 17.0; PRODUCT_BUNDLE_IDENTIFIER = org.opencnc3d.ios.MobileControlsUITests; PRODUCT_NAME = "$(TARGET_NAME)"; SDKROOT = iphoneos; SUPPORTED_PLATFORMS = iphonesimulator; TARGETED_DEVICE_FAMILY = "1,2"; FRAMEWORK_SEARCH_PATHS = ("$(inherited)","$(PLATFORM_DIR)/Developer/Library/Frameworks"); LD_RUNPATH_SEARCH_PATHS = ("$(inherited)","@executable_path/Frameworks","@loader_path/Frameworks");}; name = Debug;};
 A0000000000000000000000F = {isa = XCBuildConfiguration; buildSettings = {CLANG_ENABLE_MODULES = YES; CODE_SIGNING_ALLOWED = NO; CODE_SIGN_STYLE = Automatic; GENERATE_INFOPLIST_FILE = YES; IPHONEOS_DEPLOYMENT_TARGET = 17.0; PRODUCT_BUNDLE_IDENTIFIER = org.opencnc3d.ios.MobileControlsUITests; PRODUCT_NAME = "$(TARGET_NAME)"; SDKROOT = iphoneos; SUPPORTED_PLATFORMS = iphonesimulator; TARGETED_DEVICE_FAMILY = "1,2"; FRAMEWORK_SEARCH_PATHS = ("$(inherited)","$(PLATFORM_DIR)/Developer/Library/Frameworks"); LD_RUNPATH_SEARCH_PATHS = ("$(inherited)","@executable_path/Frameworks","@loader_path/Frameworks");}; name = Release;};
 A00000000000000000000010 = {isa = PBXBuildFile; fileRef = A00000000000000000000008;};
 };
 rootObject = A00000000000000000000001;
}
'''
(PROJECT / "project.pbxproj").write_text(pbx.replace("SOURCE_PATH", source.replace('"', '\\"')))
scheme_dir = PROJECT / "xcshareddata/xcschemes"
scheme_dir.mkdir(parents=True, exist_ok=True)
(scheme_dir / "MobileControlsUITests.xcscheme").write_text('''<?xml version="1.0" encoding="UTF-8"?>
<Scheme LastUpgradeVersion="2620" version="1.7">
 <BuildAction parallelizeBuildables="NO" buildImplicitDependencies="YES"><BuildActionEntries><BuildActionEntry buildForTesting="YES" buildForRunning="NO" buildForProfiling="NO" buildForArchiving="NO" buildForAnalyzing="YES"><BuildableReference BuildableIdentifier="primary" BlueprintIdentifier="A00000000000000000000005" BuildableName="MobileControlsUITests.xctest" BlueprintName="MobileControlsUITests" ReferencedContainer="container:MobileControlsUITests.xcodeproj"/></BuildActionEntry></BuildActionEntries></BuildAction>
 <TestAction buildConfiguration="Debug" selectedDebuggerIdentifier="Xcode.DebuggerFoundation.Debugger.LLDB" selectedLauncherIdentifier="Xcode.IDEFoundation.Launcher.LLDB" shouldUseLaunchSchemeArgsEnv="YES"><Testables><TestableReference skipped="NO"><BuildableReference BuildableIdentifier="primary" BlueprintIdentifier="A00000000000000000000005" BuildableName="MobileControlsUITests.xctest" BlueprintName="MobileControlsUITests" ReferencedContainer="container:MobileControlsUITests.xcodeproj"/></TestableReference></Testables></TestAction>
</Scheme>
''')
print(PROJECT)
