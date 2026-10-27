#import <XCTest/XCTest.h>
#import <UIKit/UIKit.h>

/* Native system-generated HID events, not synthetic SDL queue events. */
@interface MobileControlsUITests : XCTestCase
@property(nonatomic, strong) XCUIApplication *game;
@end

@implementation MobileControlsUITests

- (XCUIElement *)element:(NSString *)identifier {
    return [[[self.game descendantsMatchingType:XCUIElementTypeAny] matchingIdentifier:identifier] firstMatch];
}

- (void)setUp {
    [super setUp];
    self.continueAfterFailure = NO;
    [XCUIDevice sharedDevice].orientation = UIDeviceOrientationLandscapeLeft;
    self.game = [[XCUIApplication alloc] initWithBundleIdentifier:@"org.opencnc3d.ios.development"];
    self.game.launchArguments = @[@"--mobile-fixture", @"--nosound"];
    [self.game launch];
    XCTAssertTrue([[self element:@"mobile.build"] waitForExistenceWithTimeout:90], @"Native mobile overlay did not become ready");
}

- (void)tearDown {
    [self.game terminate];
    self.game = nil;
    [super tearDown];
}

- (NSDictionary *)status {
    XCUIElement *status = [self element:@"mobile.status"];
    id value = status.value;
    NSString *json = [value isKindOfClass:[NSString class]] ? value : status.label;
    NSData *data = [json dataUsingEncoding:NSUTF8StringEncoding];
    id state = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
    XCTAssertTrue([state isKindOfClass:[NSDictionary class]], @"mobile.status must expose the agreed diagnostic JSON as accessibilityValue: %@", json);
    return [state isKindOfClass:[NSDictionary class]] ? state : @{};
}

- (void)waitForStatus:(BOOL (^)(NSDictionary *))condition timeout:(NSTimeInterval)timeout description:(NSString *)description {
    NSPredicate *predicate = [NSPredicate predicateWithBlock:^BOOL(id object, NSDictionary *bindings) {
        return condition([self status]);
    }];
    XCTestExpectation *expected = [self expectationForPredicate:predicate evaluatedWithObject:self handler:nil];
    (void)description;
    [self waitForExpectations:@[expected] timeout:timeout];
}

- (void)attachScreenshot:(NSString *)name {
    XCTAttachment *shot = [XCTAttachment attachmentWithScreenshot:[[XCUIScreen mainScreen] screenshot]];
    shot.name = name;
    shot.lifetime = XCTAttachmentLifetimeKeepAlways;
    [self addAttachment:shot];
}

- (void)logStatus:(NSDictionary *)state name:(NSString *)name {
    NSData *data = [NSJSONSerialization dataWithJSONObject:state options:NSJSONWritingSortedKeys error:NULL];
    NSString *json = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    NSLog(@"MOBILESTATE|%@|%@", name, json);
    XCTAttachment *attachment = [XCTAttachment attachmentWithString:json];
    attachment.name = name;
    attachment.lifetime = XCTAttachmentLifetimeKeepAlways;
    [self addAttachment:attachment];
}

- (void)testNativeOverlayExists {
    for (NSString *identifier in @[@"mobilesurface", @"mobile.sidebar", @"mobile.minimap", @"mobile.orders", @"mobile.build", @"mobile.status"]) {
        XCTAssertTrue([self element:identifier].exists, @"Missing native overlay element %@", identifier);
    }
    XCTAssertTrue([self element:@"mobile.build"].hittable);
    [self attachScreenshot:@"native-mobile-overlay"];
}

- (NSDictionary *)buildPowerPlantAndEnterPlacement {
    [[self element:@"mobile.build"] tap];
    XCUIElement *power = [self element:@"build.NUKE"];
    XCTAssertTrue([power waitForExistenceWithTimeout:10], @"Power Plant build entry missing");
    [power tap];
    [self waitForStatus:^BOOL(NSDictionary *state) {
        return [@[@"ready", @"placing"] containsObject:state[@"construction_state"]];
    } timeout:30 description:@"Power Plant becomes ready for placement"];
    if (![[self status][@"placing"] boolValue]) [[self element:@"mobile.build"] tap];
    [self waitForStatus:^BOOL(NSDictionary *state) {
        return [state[@"placing"] boolValue] && [state[@"placement_valid"] boolValue];
    } timeout:10 description:@"Ready building enters placement with a legal fixture target"];
    return [self status];
}

- (void)testTwoSeparateTapsConstructOneBuilding {
    NSDictionary *initial = [self status];
    XCTAssertNotNil(initial[@"placed_count"], @"Fixture must expose placed_count");
    NSInteger before = [initial[@"placed_count"] integerValue];
    [self buildPowerPlantAndEnterPlacement];
    NSDictionary *ready = [self status];
    XCTAssertNotNil(ready[@"placement_x"]);
    XCTAssertNotNil(ready[@"placement_y"]);
    double x = [ready[@"placement_x"] doubleValue], y = [ready[@"placement_y"] doubleValue];
    XCTAssertTrue(x > 0 && x < 1 && y > 0 && y < 1, @"Fixture must provide a legal placement point normalized to mobilesurface");
    XCUICoordinate *point = [[self element:@"mobilesurface"] coordinateWithNormalizedOffset:CGVectorMake(x, y)];
    [point tap];
    [self waitForStatus:^BOOL(NSDictionary *state) {
        return [state[@"ghost_committed"] boolValue] == NO && [state[@"choice_selected"] boolValue];
    } timeout:10 description:@"First contact positions and primes a preview"];
    XCTAssertEqual([[self status][@"placed_count"] integerValue], before, @"First tap must not construct");
    [self attachScreenshot:@"first-tap-preview"];
    /* Separate touch sequence. Deliberately no doubleTap() shortcut or timer. */
    [point tap];
    [self waitForStatus:^BOOL(NSDictionary *state) {
        return [state[@"placed_count"] integerValue] == before + 1;
    } timeout:10 description:@"Second contact commits exactly one building"];
    [self attachScreenshot:@"second-tap-constructed"];
}

- (void)testCancelPlacementKeepsReadyBuilding {
    NSDictionary *initial = [self status];
    NSInteger before = [initial[@"placed_count"] integerValue];
    [self buildPowerPlantAndEnterPlacement];
    XCTAssertTrue([self element:@"mobile.cancel"].hittable);
    [[self element:@"mobile.cancel"] tap];
    [self waitForStatus:^BOOL(NSDictionary *state) {
        return ![state[@"placing"] boolValue] && [state[@"construction_state"] isEqual:@"ready"];
    } timeout:10 description:@"Cancel keeps completed production ready"];
    XCTAssertEqual([[self status][@"placed_count"] integerValue], before);
    XCTAssertEqual([[self status][@"placement_requests"] integerValue], 0);
    [[self element:@"mobile.build"] tap];
    [self waitForStatus:^BOOL(NSDictionary *state) {
        return [state[@"placing"] boolValue] && [state[@"construction_state"] isEqual:@"ready"];
    } timeout:10 description:@"The same completed building can be positioned again"];
    [self attachScreenshot:@"cancel-keeps-ready"];
}

- (void)testMinimapNavigatesAndOrdersCenterCancels {
    [[self element:@"mobile.all"] tap];
    [self waitForStatus:^BOOL(NSDictionary *state) { return [state[@"selected_count"] integerValue] > 0; }
                 timeout:10 description:@"All selects real mission units"];
    NSInteger selected = [[self status][@"selected_count"] integerValue];
    [[self element:@"mobilesurface"] pinchWithScale:2 velocity:1];
    NSDictionary *before = [self status];
    [self logStatus:before name:@"radar-before"];
    NSInteger orderRequests = [before[@"order_requests"] integerValue], stopRequests = [before[@"stop_requests"] integerValue];
    double startX = [before[@"camera_x"] doubleValue], startY = [before[@"camera_y"] doubleValue];
    XCUICoordinate *radar = [[self element:@"mobile.minimap"] coordinateWithNormalizedOffset:CGVectorMake(.20, .20)];
    [radar tap];
    [self waitForStatus:^BOOL(NSDictionary *state) {
        NSLog(@"MOBILESTATE|radar-after-camera|x=%@|y=%@", state[@"camera_x"], state[@"camera_y"]);
        return fabs([state[@"camera_x"] doubleValue] - startX) > .1 || fabs([state[@"camera_y"] doubleValue] - startY) > .1;
    } timeout:10 description:@"Native minimap contact moves the real camera"];
    [self logStatus:[self status] name:@"radar-after"];
    XCTAssertEqual([[self status][@"selected_count"] integerValue], selected, @"Minimap navigation preserves selection");
    [[self element:@"mobile.orders"] pressForDuration:.4];
    [self waitForStatus:^BOOL(NSDictionary *state) { return ![state[@"compass_open"] boolValue]; }
                 timeout:10 description:@"Releasing the center cancels Orders"];
    XCTAssertEqual([[self status][@"selected_count"] integerValue], selected);
    XCTAssertEqual([[self status][@"order_requests"] integerValue], orderRequests, @"Minimap and center cancellation must not order units");
    XCTAssertEqual([[self status][@"stop_requests"] integerValue], stopRequests, @"Center release must not issue Stop");
    [self attachScreenshot:@"minimap-and-orders-center-cancel"];
}

- (void)testTapRealUnitThenHoldDragMove {
    NSDictionary *initial = [self status];
    [self logStatus:initial name:@"unit-tap-target"];
    XCTAssertNotNil(initial[@"first_mobile_x"]);
    XCTAssertNotNil(initial[@"first_mobile_y"]);
    double x = [initial[@"first_mobile_x"] doubleValue], y = [initial[@"first_mobile_y"] doubleValue];
    XCTAssertTrue(x > 0 && x < 1 && y > 0 && y < 1, @"Fixture must expose a visible real unit point");
    XCUICoordinate *unit = [[self element:@"mobilesurface"] coordinateWithNormalizedOffset:CGVectorMake(x,y)];
    [unit tap];
    [self waitForStatus:^BOOL(NSDictionary *state) { return [state[@"selected_count"] integerValue] > 0; }
                 timeout:10 description:@"Touching an actual infantry silhouette selects it"];
    NSInteger orders = [[self status][@"order_requests"] integerValue];
    XCUICoordinate *handle = [[self element:@"mobile.orders"] coordinateWithNormalizedOffset:CGVectorMake(.5,.5)];
    XCUICoordinate *left = [handle coordinateWithOffset:CGVectorMake(-80,0)];
    [handle pressForDuration:.35 thenDragToCoordinate:left];
    XCTAssertEqual([[self status][@"order_requests"] integerValue], orders, @"Selecting Move only arms a destination");
    XCUICoordinate *ground = [[self element:@"mobilesurface"] coordinateWithNormalizedOffset:CGVectorMake(.45,.4)];
    [ground tap];
    [self waitForStatus:^BOOL(NSDictionary *state) {
        return [state[@"order_requests"] integerValue] > orders;
    } timeout:10 description:@"A separate ground tap sends the real engine command"];
    XCTAssertEqual([[self status][@"last_order_mode"] integerValue], 1, @"Left directional release must produce Move Only");
    [self attachScreenshot:@"real-unit-directional-move"];
}

- (void)testNavigationClearsPlacementConfirmation {
    NSDictionary *initial = [self status];
    NSInteger before = [initial[@"placed_count"] integerValue];
    NSInteger requests = [initial[@"placement_requests"] integerValue];
    NSDictionary *ready = [self buildPowerPlantAndEnterPlacement];
    XCUICoordinate *point = [[self element:@"mobilesurface"] coordinateWithNormalizedOffset:CGVectorMake([ready[@"placement_x"] doubleValue], [ready[@"placement_y"] doubleValue])];
    [point tap];
    [self waitForStatus:^BOOL(NSDictionary *state) { return [state[@"choice_selected"] boolValue]; }
                 timeout:10 description:@"First tap primes construction before navigation"];
    XCUICoordinate *start = [[self element:@"mobilesurface"] coordinateWithNormalizedOffset:CGVectorMake(.30,.30)];
    XCUICoordinate *end = [[self element:@"mobilesurface"] coordinateWithNormalizedOffset:CGVectorMake(.35,.34)];
    [start pressForDuration:.05 thenDragToCoordinate:end];
    [self waitForStatus:^BOOL(NSDictionary *state) { return ![state[@"choice_selected"] boolValue]; }
                 timeout:10 description:@"A field pan clears construction confirmation"];
    XCTAssertEqual([[self status][@"placement_requests"] integerValue], requests);
    XCTAssertEqual([[self status][@"placed_count"] integerValue], before);
    [self waitForStatus:^BOOL(NSDictionary *state) { return [state[@"placement_valid"] boolValue]; }
                 timeout:10 description:@"A legal placement remains available after the pan"];
    NSDictionary *afterPan = [self status];
    point = [[self element:@"mobilesurface"] coordinateWithNormalizedOffset:CGVectorMake([afterPan[@"placement_x"] doubleValue], [afterPan[@"placement_y"] doubleValue])];
    [point tap];
    [self waitForStatus:^BOOL(NSDictionary *state) { return [state[@"choice_selected"] boolValue]; }
                 timeout:10 description:@"A fresh tap can prime the moved view"];
    [[self element:@"mobilesurface"] pinchWithScale:1.1 velocity:1];
    [self waitForStatus:^BOOL(NSDictionary *state) { return ![state[@"choice_selected"] boolValue]; }
                 timeout:10 description:@"A real two-finger pinch clears construction confirmation"];
    XCTAssertEqual([[self status][@"placement_requests"] integerValue], requests);
    XCTAssertEqual([[self status][@"placed_count"] integerValue], before);
    [self logStatus:[self status] name:@"navigation-clears-placement"];
    [self attachScreenshot:@"navigation-cleared-construction-confirmation"];
}

@end
