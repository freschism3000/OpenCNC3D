#import <UIKit/UIKit.h>
#include <SDL_syswm.h>
#include "ios_controls.h"
#include "ios_game_bridge.h"
#include "ios_touch_logic.h"
#include <algorithm>
#include <cmath>

static UIColor *Ink(){return [UIColor colorWithRed:.9 green:.93 blue:.84 alpha:1];}
static UIColor *Gold(){return [UIColor colorWithRed:.91 green:.79 blue:.48 alpha:1];}
static UIColor *Panel(){return [UIColor colorWithRed:.12 green:.19 blue:.16 alpha:.97];}
static UIColor *Line(){return [UIColor colorWithRed:.44 green:.53 blue:.43 alpha:1];}
static void Text(NSString *s,CGRect r,CGFloat size,UIColor *color,NSTextAlignment align=NSTextAlignmentLeft){
    NSMutableParagraphStyle *p=[NSMutableParagraphStyle new];p.alignment=align;p.lineBreakMode=NSLineBreakByTruncatingTail;
    [s drawInRect:r withAttributes:@{NSFontAttributeName:[UIFont monospacedSystemFontOfSize:size weight:UIFontWeightMedium],NSForegroundColorAttributeName:color,NSParagraphStyleAttributeName:p}];
}
static void Plate(CGRect r,UIColor *fill,UIColor *stroke,CGFloat radius=5){
    UIBezierPath *p=[UIBezierPath bezierPathWithRoundedRect:r cornerRadius:radius];[fill setFill];[p fill];[stroke setStroke];p.lineWidth=1;[p stroke];
}
static NSString *BuildTitle(const char *name){
    NSDictionary *titles=@{@"NUKE":@"Power plant",@"PYLE":@"Barracks",@"PROC":@"Refinery",@"FACT":@"Construction yard",@"WEAP":@"War factory",@"HAND":@"Hand of Nod",@"SILO":@"Silo",@"HQ":@"Radar",@"E1":@"Rifle infantry",@"JEEP":@"Humvee",@"HTNK":@"Mammoth tank"};
    NSString *key=[NSString stringWithUTF8String:name];return titles[key]?:key;
}

@class CNCPhoneControls;
@interface CNCRadar : UIControl
@property(nonatomic,weak) CNCPhoneControls *owner;
@property(nonatomic,strong) UIImage *image;
@property(nonatomic) CGRect mapRect;
@property(nonatomic,strong) UIBezierPath *viewport;
@end

@interface CNCPhoneControls : UIView<UIGestureRecognizerDelegate> {
    SDL_Window *_window;
    CNC_IOSGameSnapshot _game;
    CNC_IOSPlacement _place;
    cnc_touch::PlacementChoice _choice;
    uint64_t _viewRevision,_recipients;
    CGPoint _previousPan,_compassOrigin,_compassVisual,_commandTarget,_lastTap;
    CGSize _lastSize;
    float _lastCameraX,_lastCameraY;
    BOOL _compass,_compassOnMap,_selecting,_drawerOpen,_multitouch,_modalForwarding;
    int _direction,_armedOrder,_constructionRequests,_placementRequests;
    CGRect _sidebarRect,_deckRect,_fieldRect;
    CFTimeInterval _noticeUntil;
    NSString *_notice;
    UIButton *_build,*_cancel,*_orders,*_stop,*_select,*_all,*_rifles,*_armor,*_base;
    UIView *_sidebar;
    UILabel *_status;
    UISelectionFeedbackGenerator *_selectionHaptic;
    UIImpactFeedbackGenerator *_confirmHaptic;
    CNCRadar *_radar;
    UIScrollView *_drawer;
    NSString *_catalogSignature;
    UIPanGestureRecognizer *_pan;
    UIPinchGestureRecognizer *_pinch;
    UILongPressGestureRecognizer *_hold;
    UILongPressGestureRecognizer *_twoTouch;
}
- (instancetype)initWithWindow:(SDL_Window*)window frame:(CGRect)frame;
- (void)refresh;
- (void)interrupt;
- (void)navigateRadar:(CGPoint)point;
@end

@implementation CNCRadar
- (instancetype)initWithFrame:(CGRect)frame {
    if((self=[super initWithFrame:frame])){
        self.accessibilityIdentifier=@"mobile.minimap";self.accessibilityLabel=@"Minimap. Drag to look around";
        self.isAccessibilityElement=YES;self.multipleTouchEnabled=NO;
    }return self;
}
- (void)drawRect:(CGRect)rect {
    Plate(self.bounds,Panel(),Line(),3);
    if(self.image)[self.image drawInRect:self.bounds];
    CGContextRef ctx=UIGraphicsGetCurrentContext();CGContextSaveGState(ctx);CGContextClipToRect(ctx,self.mapRect);
    [Gold() setStroke];self.viewport.lineWidth=1.5;[self.viewport stroke];CGContextRestoreGState(ctx);
    Text(@"RADAR",CGRectMake(5,4,self.bounds.size.width-10,12),9,Ink());
}
- (BOOL)beginTrackingWithTouch:(UITouch*)touch withEvent:(UIEvent*)event {
    CGPoint p=[touch locationInView:self];if(!CGRectContainsPoint(self.mapRect,p))return NO;
    [self.owner navigateRadar:p];return YES;
}
- (BOOL)continueTrackingWithTouch:(UITouch*)touch withEvent:(UIEvent*)event {
    CGPoint p=[touch locationInView:self];if(CGRectContainsPoint(self.mapRect,p))[self.owner navigateRadar:p];return YES;
}
@end

@implementation CNCPhoneControls
- (UIButton*)button:(NSString*)title identifier:(NSString*)identifier action:(SEL)action {
    UIButton *b=[UIButton buttonWithType:UIButtonTypeCustom];[b setTitle:title forState:UIControlStateNormal];
    b.titleLabel.font=[UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightSemibold];
    [b setTitleColor:Ink() forState:UIControlStateNormal];[b setTitleColor:Gold() forState:UIControlStateHighlighted];
    b.backgroundColor=[UIColor colorWithRed:.21 green:.29 blue:.23 alpha:.98];
    b.layer.borderColor=Line().CGColor;b.layer.borderWidth=1;b.layer.cornerRadius=4;
    b.accessibilityIdentifier=identifier;[b addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];[self addSubview:b];return b;
}
- (instancetype)initWithWindow:(SDL_Window*)window frame:(CGRect)frame {
    if((self=[super initWithFrame:frame])){
        _window=window;self.backgroundColor=UIColor.clearColor;self.opaque=NO;self.multipleTouchEnabled=YES;
        self.accessibilityIdentifier=@"mobilesurface";self.isAccessibilityElement=NO;
        self.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
        _direction=-1;_armedOrder=-1;
        _selectionHaptic=[UISelectionFeedbackGenerator new];_confirmHaptic=[[UIImpactFeedbackGenerator alloc]initWithStyle:UIImpactFeedbackStyleLight];
        _sidebar=[UIView new];_sidebar.userInteractionEnabled=NO;_sidebar.accessibilityIdentifier=@"mobile.sidebar";_sidebar.accessibilityLabel=@"Tactical sidebar";_sidebar.isAccessibilityElement=YES;[self addSubview:_sidebar];
        _build=[self button:@"Build" identifier:@"mobile.build" action:@selector(buildTap)];
        _base=[self button:@"Base" identifier:@"mobile.base" action:@selector(baseTap)];
        _cancel=[self button:@"Cancel placement" identifier:@"mobile.cancel" action:@selector(cancelTap)];
        _orders=[self button:@"Orders  ◇" identifier:@"mobile.orders" action:@selector(ordersTap)];
        _stop=[self button:@"Stop" identifier:@"mobile.stop" action:@selector(stopTap)];
        _select=[self button:@"Box select" identifier:@"mobile.select" action:@selector(selectTap)];
        _all=[self button:@"All" identifier:@"mobile.all" action:@selector(allTap)];
        _rifles=[self button:@"Infantry" identifier:@"mobile.infantry" action:@selector(riflesTap)];
        _armor=[self button:@"Vehicles" identifier:@"mobile.vehicles" action:@selector(armorTap)];
        _radar=[[CNCRadar alloc]initWithFrame:CGRectZero];_radar.owner=self;[self addSubview:_radar];
        _status=[UILabel new];_status.font=[UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightMedium];_status.textColor=Ink();
        _status.accessibilityIdentifier=@"mobile.status";_status.accessibilityLabel=@"Mobile status";[self addSubview:_status];
        _drawer=[[UIScrollView alloc]initWithFrame:CGRectZero];_drawer.backgroundColor=Panel();_drawer.layer.borderColor=Line().CGColor;
        _drawer.layer.borderWidth=1;_drawer.layer.cornerRadius=6;_drawer.hidden=YES;_drawer.accessibilityIdentifier=@"mobile.catalog";[self addSubview:_drawer];
        UITapGestureRecognizer *tap=[[UITapGestureRecognizer alloc]initWithTarget:self action:@selector(mapTap:)];tap.delegate=self;tap.cancelsTouchesInView=NO;[self addGestureRecognizer:tap];
        _pan=[[UIPanGestureRecognizer alloc]initWithTarget:self action:@selector(mapPan:)];_pan.delegate=self;_pan.maximumNumberOfTouches=2;[self addGestureRecognizer:_pan];
        _pinch=[[UIPinchGestureRecognizer alloc]initWithTarget:self action:@selector(mapPinch:)];_pinch.delegate=self;[self addGestureRecognizer:_pinch];
        _hold=[[UILongPressGestureRecognizer alloc]initWithTarget:self action:@selector(mapHold:)];_hold.minimumPressDuration=.28;_hold.allowableMovement=12;_hold.delegate=self;[self addGestureRecognizer:_hold];
        _pan.cancelsTouchesInView=NO;_pinch.cancelsTouchesInView=NO;_hold.cancelsTouchesInView=NO;
        [tap requireGestureRecognizerToFail:_hold];[_pan requireGestureRecognizerToFail:_hold];
        _twoTouch=[[UILongPressGestureRecognizer alloc]initWithTarget:self action:@selector(twoTouches:)];_twoTouch.numberOfTouchesRequired=2;_twoTouch.minimumPressDuration=0;_twoTouch.delegate=self;_twoTouch.cancelsTouchesInView=NO;[self addGestureRecognizer:_twoTouch];
        UILongPressGestureRecognizer *ordersHold=[[UILongPressGestureRecognizer alloc]initWithTarget:self action:@selector(ordersHold:)];ordersHold.minimumPressDuration=.22;[_orders addGestureRecognizer:ordersHold];
        [[NSNotificationCenter defaultCenter]addObserver:self selector:@selector(interrupt) name:UIApplicationWillResignActiveNotification object:nil];
        cnc_ios_game_set_native_hud(1);[self refresh];
    }return self;
}
- (void)dealloc {[[NSNotificationCenter defaultCenter]removeObserver:self];}
- (CGPoint)pixels:(CGPoint)p {
    return CGPointMake(p.x*_game.drawable_width/MAX(1.,self.bounds.size.width),p.y*_game.drawable_height/MAX(1.,self.bounds.size.height));
}
- (CGPoint)points:(CGPoint)p {
    return CGPointMake(p.x*self.bounds.size.width/MAX(1,_game.drawable_width),p.y*self.bounds.size.height/MAX(1,_game.drawable_height));
}
- (void)notice:(NSString*)text {
    _notice=text;_noticeUntil=CACurrentMediaTime()+3;[self setNeedsDisplay];
}
- (void)clearChoice {_choice.clear();}
- (void)interrupt {
    [self clearChoice];_compass=NO;_direction=-1;_armedOrder=-1;_selecting=NO;_multitouch=YES;
    [self setNeedsDisplay];
}
- (BOOL)inField:(CGPoint)p {return CGRectContainsPoint(_fieldRect,p)&&!(_drawerOpen&&CGRectContainsPoint(_drawer.frame,p));}
- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat w=self.bounds.size.width,h=self.bounds.size.height;UIEdgeInsets s=self.safeAreaInsets;
    BOOL compact=h<600;CGFloat side=compact?154:188;CGFloat right=w-MAX(s.right,8.);
    _sidebarRect=CGRectMake(right-side,MAX(s.top,8.),side,h-MAX(s.top,8.)-MAX(s.bottom,8.));
    _sidebar.frame=_sidebarRect;
    CGFloat deckHeight=compact?110:112;
    _deckRect=CGRectMake(MAX(s.left,8.),h-MAX(s.bottom,8.)-deckHeight,CGRectGetMinX(_sidebarRect)-MAX(s.left,8.)-8,deckHeight);
    _fieldRect=CGRectMake(MAX(s.left,8.),MAX(s.top,8.),_deckRect.size.width,CGRectGetMinY(_deckRect)-MAX(s.top,8.)-6);
    _radar.frame=CGRectMake(_sidebarRect.origin.x+8,_sidebarRect.origin.y+26,side-16,side-16);
    CGFloat y=CGRectGetMaxY(_radar.frame)+52;
    _build.frame=CGRectMake(_sidebarRect.origin.x+8,y,side-16,46);
    _cancel.frame=CGRectMake(_sidebarRect.origin.x+8,y+54,side-16,46);
    _base.frame=CGRectMake(_sidebarRect.origin.x+8,CGRectGetMaxY(_sidebarRect)-52,side-16,44);
    CGFloat gap=6,cell=(_deckRect.size.width-16-2*gap)/3.;
    _all.frame=CGRectMake(_deckRect.origin.x+8,_deckRect.origin.y+8,cell,44);
    _rifles.frame=CGRectOffset(_all.frame,cell+gap,0);_armor.frame=CGRectOffset(_rifles.frame,cell+gap,0);
    _select.frame=CGRectMake(_deckRect.origin.x+8,CGRectGetMaxY(_all.frame)+6,cell,44);
    _orders.frame=CGRectOffset(_select.frame,cell+gap,0);_stop.frame=CGRectOffset(_orders.frame,cell+gap,0);
    _status.frame=CGRectMake(_fieldRect.origin.x+8,_fieldRect.origin.y+6,_fieldRect.size.width-16,23);
    CGFloat dw=MIN(286.,_fieldRect.size.width-12);
    _drawer.frame=CGRectMake(CGRectGetMinX(_sidebarRect)-dw-8,_sidebarRect.origin.y+36,dw,MIN(h-MAX(s.bottom,8.)-_sidebarRect.origin.y-44,408.));
    if(!CGSizeEqualToSize(_lastSize,self.bounds.size)){_lastSize=self.bounds.size;_viewRevision++;[self interrupt];_catalogSignature=nil;}
    CGPoint fieldOrigin=[self pixels:_fieldRect.origin],fieldEnd=[self pixels:CGPointMake(CGRectGetMaxX(_fieldRect),CGRectGetMaxY(_fieldRect))];
    cnc_ios_game_set_field_rect(fieldOrigin.x,fieldOrigin.y,fieldEnd.x-fieldOrigin.x,fieldEnd.y-fieldOrigin.y);
}
- (BOOL)gestureRecognizer:(UIGestureRecognizer*)g shouldReceiveTouch:(UITouch*)touch {
    if(touch.phase==UITouchPhaseBegan && _multitouch && [g numberOfTouches]==0)_multitouch=NO;
    UIView *v=touch.view;
    while(v&&v!=self){if([v isKindOfClass:UIControl.class]||v==_drawer)return NO;v=v.superview;}
    CGPoint p=[touch locationInView:self];
    return !_game.modal&&[self inField:p];
}
- (BOOL)gestureRecognizer:(UIGestureRecognizer*)a shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer*)b {
    if(a==_twoTouch||b==_twoTouch)return YES;
    if((a==_hold&&b==_pinch)||(b==_hold&&a==_pinch))return YES;
    return (a==_pan&&b==_pinch)||(a==_pinch&&b==_pan);
}
- (void)twoTouches:(UILongPressGestureRecognizer*)g {
    if(g.state==UIGestureRecognizerStateBegan){[self interrupt];}
}
- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    if(event.allTouches.count>1){_multitouch=YES;[self clearChoice];_compass=NO;_direction=-1;}
    [super touchesBegan:touches withEvent:event];
}
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {[self interrupt];[super touchesCancelled:touches withEvent:event];}
- (BOOL)placementVisible:(const CNC_IOSPlacement&)p {
    if(!p.active||!p.legal||p.cells_truncated||!p.cell_count)return NO;
    CGRect safe=CGRectInset(_fieldRect,3,3);
    for(int i=0;i<p.cell_count;i++)for(int j=0;j<4;j++){
        CGPoint pt=[self points:CGPointMake(p.cells[i].screen_x[j],p.cells[i].screen_y[j])];
        if(!CGRectContainsPoint(safe,pt)||(_drawerOpen&&CGRectContainsPoint(_drawer.frame,pt)))return NO;
    }return YES;
}
- (void)mapTap:(UITapGestureRecognizer*)g {
    if(g.state!=UIGestureRecognizerStateEnded||_multitouch)return;
    [self refresh];
    CGPoint p=[g locationInView:self];if(![self inField:p])return;
    CGPoint px=[self pixels:p];_lastTap=p;
    if(_game.placing){
        CNC_IOSPlacement candidate={};cnc_ios_game_placement_query(px.x,px.y,&candidate);_place=candidate;
        if(![self placementVisible:candidate]){[self clearChoice];[self notice:candidate.legal?@"Move the footprint into the clear field":[NSString stringWithUTF8String:candidate.reason]];return;}
        if(_choice.matches(candidate.build_type,candidate.build_id,candidate.cell_x,candidate.cell_y,_viewRevision)){
            [self clearChoice];
            if(cnc_ios_game_placement_confirm(candidate.cell_x,candidate.cell_y)){
                [_confirmHaptic impactOccurred];
                _placementRequests++;[self notice:@"Building placement sent"];fprintf(stderr,"IOS|touch|confirm|cell=%d,%d\n",candidate.cell_x,candidate.cell_y);
            }else [self notice:@"Location changed. Choose a clear position"];
        }else{
            _choice.select(candidate.build_type,candidate.build_id,candidate.cell_x,candidate.cell_y,_viewRevision);
            [self notice:@"Tap this position again to build"];fprintf(stderr,"IOS|touch|choose|cell=%d,%d\n",candidate.cell_x,candidate.cell_y);
        }
    }else if(_selecting){cnc_ios_game_select(px.x,px.y,0);_selecting=NO;}
    else if(_armedOrder>=0){
        if(cnc_ios_game_order(px.x,px.y,_armedOrder))_armedOrder=-1;
        else [self notice:@"Choose a visible enemy target"];
    }
    else cnc_ios_game_tap(px.x,px.y);
    [self refresh];
}
- (void)mapPan:(UIPanGestureRecognizer*)g {
    CGPoint p=[g locationInView:self];
    CGPoint delta=[g translationInView:self];
    if(g.state==UIGestureRecognizerStateBegan){
        [self clearChoice];_compass=NO;_armedOrder=-1;_viewRevision++;
        _previousPan=CGPointMake(p.x-delta.x,p.y-delta.y);
        if(_selecting){_commandTarget=_previousPan;_lastTap=p;}
    }
    if(g.numberOfTouches>1){_multitouch=YES;_selecting=NO;[self clearChoice];}
    if(g.state==UIGestureRecognizerStateChanged||g.state==UIGestureRecognizerStateBegan){
        if(_selecting&&g.numberOfTouches==1){_lastTap=p;[self setNeedsDisplay];}
        else {CGPoint a=[self pixels:CGPointMake(p.x-delta.x,p.y-delta.y)],b=[self pixels:p];cnc_ios_game_pan(a.x,a.y,b.x,b.y);}
        [g setTranslation:CGPointZero inView:self];
    }
    if(g.state==UIGestureRecognizerStateEnded&&_selecting){
        CGPoint a=[self pixels:_commandTarget],b=[self pixels:p];cnc_ios_game_select_box(a.x,a.y,b.x,b.y,0);_selecting=NO;
    }
    if(g.state==UIGestureRecognizerStateCancelled){_selecting=NO;[self clearChoice];}
    [self refresh];
}
- (void)mapPinch:(UIPinchGestureRecognizer*)g {
    [self interrupt];_viewRevision++;
    if(g.state==UIGestureRecognizerStateBegan||g.state==UIGestureRecognizerStateChanged){
        CGPoint p=[self pixels:[g locationInView:self]];cnc_ios_game_zoom(g.scale,p.x,p.y);g.scale=1;
    }[self refresh];
}
- (void)beginCompass:(CGPoint)p onMap:(BOOL)onMap {
    [self clearChoice];_armedOrder=-1;
    if(!_game.selected_count||_game.placing){[self notice:@"Select units first"];return;}
    [self clearChoice];_compass=YES;_compassOnMap=onMap;_compassOrigin=p;_commandTarget=p;_direction=-1;_recipients=_game.selection_signature;
    [_selectionHaptic prepare];
    _compassVisual=CGPointMake(std::max(CGRectGetMinX(_fieldRect)+112.,std::min(CGRectGetMaxX(_fieldRect)-112.,p.x)),std::max(CGRectGetMinY(_fieldRect)+105.,std::min(CGRectGetMaxY(_fieldRect)-105.,p.y)));
    [self setNeedsDisplay];
}
- (void)trackCompass:(CGPoint)p state:(UIGestureRecognizerState)state {
    if(!_compass)return;
    int next=cnc_touch::direction(p.x-_compassOrigin.x,p.y-_compassOrigin.y,_direction);
    if(next!=_direction&&next>=0)[_selectionHaptic selectionChanged];_direction=next;
    if(state==UIGestureRecognizerStateEnded){
        int d=_direction;_compass=NO;_direction=-1;
        CNC_IOSGameSnapshot current={};cnc_ios_game_snapshot(&current);
        if(current.selection_signature!=_recipients){[self notice:@"Selection changed. Command cancelled"];return;}
        if(d==2){cnc_ios_game_stop();[self notice:@"Stop"];}
        else if(d>=0){
            int mode=d==0?CNC_IOS_ORDER_ATTACK_MOVE:d==1?CNC_IOS_ORDER_FOCUS_FIRE:CNC_IOS_ORDER_MOVE_ONLY;
            if(_compassOnMap){CGPoint px=[self pixels:_commandTarget];BOOL sent=cnc_ios_game_order(px.x,px.y,mode);[self notice:!sent?@"Choose a visible enemy target":d==0?@"Advance":d==1?@"Focus fire":@"Move"];}
            else{_armedOrder=mode;[self notice:@"Tap a destination or target"];}
        }else [self notice:@"Command cancelled"];
        fprintf(stderr,"IOS|touch|compass-release|direction=%d\n",d);
    }else if(state==UIGestureRecognizerStateCancelled||state==UIGestureRecognizerStateFailed){_compass=NO;_direction=-1;}
    [self refresh];
}
- (void)mapHold:(UILongPressGestureRecognizer*)g {
    if(g.state==UIGestureRecognizerStateBegan)[self beginCompass:[g locationInView:self] onMap:YES];
    else [self trackCompass:[g locationInView:self] state:g.state];
}
- (void)ordersHold:(UILongPressGestureRecognizer*)g {
    if(g.state==UIGestureRecognizerStateBegan)[self beginCompass:[g locationInView:self] onMap:NO];
    else [self trackCompass:[g locationInView:self] state:g.state];
}
- (void)ordersTap {[self notice:@"Hold Orders, drag a direction, then tap a target"];}
- (void)baseTap {[self interrupt];cnc_ios_game_focus_base();_viewRevision++;[self refresh];}
- (void)stopTap {[self clearChoice];cnc_ios_game_stop();_armedOrder=-1;_compass=NO;[self refresh];}
- (void)selectTap {
    [self clearChoice];_selecting=!_selecting;[self notice:_selecting?@"Drag a box around your units":@"Drag to pan"];
}
- (void)allTap {[self clearChoice];_compass=NO;cnc_ios_game_select_visible(0);[self refresh];}
- (void)riflesTap {[self clearChoice];_compass=NO;cnc_ios_game_select_visible(1);[self refresh];}
- (void)armorTap {[self clearChoice];_compass=NO;cnc_ios_game_select_visible(2);[self refresh];}
- (void)buildTap {
    _armedOrder=-1;_compass=NO;_selecting=NO;
    if(!_game.placing){
        for(int i=0;i<_game.build_count;i++){
            const auto &b=_game.builds[i];
            if(b.is_structure&&b.completed){
                [self clearChoice];cnc_ios_game_build_action(b.build_type,b.build_id,CNC_IOS_BUILD_PLACE_READY);
                _drawerOpen=NO;_drawer.hidden=YES;[self notice:@"Tap a position. Tap it again to build."];[self refresh];return;
            }
        }
    }
    [self clearChoice];_drawerOpen=!_drawerOpen;_drawer.hidden=!_drawerOpen;_catalogSignature=nil;[self refresh];
}
- (void)cancelTap {
    [self interrupt];cnc_ios_game_placement_cancel();memset(&_place,0,sizeof(_place));[self notice:@"Ready building kept. Tap it to place again"];[self refresh];
}
- (void)buildEntry:(UIButton*)sender {
    int type=[sender.accessibilityValue componentsSeparatedByString:@":"].firstObject.intValue;
    int ident=[sender.accessibilityValue componentsSeparatedByString:@":"].lastObject.intValue;
    CNC_IOSGameSnapshot current={};cnc_ios_game_snapshot(&current);
    for(int i=0;i<current.build_count;i++){
        const CNC_IOSBuildEntry &b=current.builds[i];if(b.build_type!=type||b.build_id!=ident)continue;
        int action=b.completed&&b.is_structure?CNC_IOS_BUILD_PLACE_READY:CNC_IOS_BUILD_START;
        if(b.constructing){[self notice:@"Construction is running"];return;}
        if(cnc_ios_game_build_action(type,ident,action)){
            if(action==CNC_IOS_BUILD_START)_constructionRequests++;
            _drawerOpen=NO;_drawer.hidden=YES;[self clearChoice];
            [self notice:action==CNC_IOS_BUILD_PLACE_READY?@"Tap a position. Tap it again to build.":@"Construction started"];
        }[self refresh];return;
    }[self notice:@"That item is no longer available"];
}
- (void)productionAction:(UIButton*)sender {
    NSArray *parts=[sender.accessibilityValue componentsSeparatedByString:@":"];
    if(parts.count!=3)return;
    [self interrupt];
    cnc_ios_game_build_action([parts[0]intValue],[parts[1]intValue],[parts[2]intValue]);_catalogSignature=nil;[self refresh];
}
- (void)navigateRadar:(CGPoint)p {
    [self interrupt];_viewRevision++;
    CGRect r=_radar.mapRect;if(!CGRectContainsPoint(r,p))return;
    cnc_ios_game_minimap((p.x-r.origin.x)/r.size.width,(p.y-r.origin.y)/r.size.height);[self refresh];
}
- (void)refreshCatalog {
    if(!_drawerOpen)return;
    NSMutableString *signature=[NSMutableString stringWithFormat:@"%.0f",_drawer.bounds.size.width];
    for(int i=0;i<_game.build_count;i++){const auto &b=_game.builds[i];[signature appendFormat:@"|%d:%d:%d:%d:%d",b.build_type,b.build_id,b.completed,b.constructing,b.on_hold];}
    if([signature isEqual:_catalogSignature])return;_catalogSignature=signature;
    for(UIView *v in _drawer.subviews)[v removeFromSuperview];
    UILabel *head=[[UILabel alloc]initWithFrame:CGRectMake(12,8,_drawer.bounds.size.width-24,26)];head.text=@"PRODUCTION";head.textColor=Gold();head.font=[UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightBold];[_drawer addSubview:head];
    CGFloat y=42;
    for(int i=0;i<_game.build_count;i++){
        const CNC_IOSBuildEntry &b=_game.builds[i];UIButton *row=[UIButton buttonWithType:UIButtonTypeCustom];
        row.frame=CGRectMake(8,y,_drawer.bounds.size.width-16,58);row.backgroundColor=[UIColor colorWithWhite:.21 alpha:1];row.layer.borderColor=Line().CGColor;row.layer.borderWidth=1;row.layer.cornerRadius=4;
        NSString *state=b.completed?@"READY · TAP TO PLACE":b.constructing?@"BUILDING":b.on_hold?@"ON HOLD · RESUME":[NSString stringWithFormat:@"$%d",b.cost];
        [row setTitle:[NSString stringWithFormat:@"%@\n%@",BuildTitle(b.name),state] forState:UIControlStateNormal];row.titleLabel.numberOfLines=2;row.titleLabel.font=[UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightMedium];[row setTitleColor:b.completed?Gold():Ink() forState:UIControlStateNormal];
        row.accessibilityIdentifier=[@"build." stringByAppendingString:[NSString stringWithUTF8String:b.name]];row.accessibilityValue=[NSString stringWithFormat:@"%d:%d",b.build_type,b.build_id];
        row.enabled=b.completed||b.on_hold||(!b.busy&&!b.constructing);row.alpha=row.enabled?1:.65;
        [row addTarget:self action:@selector(buildEntry:) forControlEvents:UIControlEventTouchUpInside];[_drawer addSubview:row];y+=66;
        if(b.is_structure&&(b.constructing||b.on_hold||b.completed)){
            NSArray *titles=b.completed?@[@"Refund"]:b.on_hold?@[@"Resume",@"Refund"]:@[@"Hold",@"Refund"];
            for(NSUInteger j=0;j<titles.count;j++){
                int action=[titles[j]isEqual:@"Hold"]?CNC_IOS_BUILD_HOLD:[titles[j]isEqual:@"Resume"]?CNC_IOS_BUILD_START:CNC_IOS_BUILD_CANCEL;
                UIButton *control=[UIButton buttonWithType:UIButtonTypeCustom];CGFloat width=(_drawer.bounds.size.width-24)/2;
                control.frame=CGRectMake(8+j*(width+8),y,width,44);control.layer.borderColor=Line().CGColor;control.layer.borderWidth=1;control.layer.cornerRadius=4;
                [control setTitle:titles[j] forState:UIControlStateNormal];[control setTitleColor:Ink() forState:UIControlStateNormal];control.titleLabel.font=[UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightMedium];
                control.accessibilityIdentifier=[@"production." stringByAppendingString:[titles[j]lowercaseString]];
                control.accessibilityValue=[NSString stringWithFormat:@"%d:%d:%d",b.build_type,b.build_id,action];
                [control addTarget:self action:@selector(productionAction:) forControlEvents:UIControlEventTouchUpInside];[_drawer addSubview:control];
            }y+=52;
        }
    }
    if(!_game.build_count){UILabel *empty=[[UILabel alloc]initWithFrame:CGRectMake(12,y,_drawer.bounds.size.width-24,70)];empty.text=@"Deploy your MCV to begin construction.";empty.numberOfLines=3;empty.textColor=Ink();empty.font=head.font;[_drawer addSubview:empty];y+=80;}
    _drawer.contentSize=CGSizeMake(_drawer.bounds.size.width,y+8);
}
- (void)refresh {
    if(!cnc_ios_game_snapshot(&_game)){[self interrupt];self.hidden=YES;return;}
    if(_game.modal)[self interrupt];
    if((BOOL)_game.modal!=_modalForwarding){
        _modalForwarding=_game.modal;SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,_modalForwarding?"1":"0");
        if(!_modalForwarding)SDL_FlushEvents(SDL_MOUSEMOTION,SDL_MOUSEBUTTONUP);
    }
    [self layoutIfNeeded];
    if(_compass&&(_game.selection_signature!=_recipients||_game.modal))_compass=NO;
    if(_armedOrder>=0&&_game.selection_signature!=_recipients)_armedOrder=-1;
    if(_game.camera_x!=_lastCameraX||_game.camera_y!=_lastCameraY){_lastCameraX=_game.camera_x;_lastCameraY=_game.camera_y;_viewRevision++;[self clearChoice];_compass=NO;_armedOrder=-1;}
    cnc_ios_game_placement_current(&_place);
    if(!_place.active||![self placementVisible:_place])[self clearChoice];
    _cancel.hidden=!_game.placing;_select.enabled=!_game.placing;_orders.enabled=!_game.placing&&_game.selected_count>0;_stop.enabled=_game.selected_count>0;
    _select.layer.borderColor=(_selecting?Gold():Line()).CGColor;
    _status.text=_game.placing?@"PLACE BUILDING · TAP A POSITION":_game.selected_count?[NSString stringWithFormat:@"%d SELECTED · %s",_game.selected_count,_game.selection_name]:@"TAP A UNIT · DRAG TO PAN · PINCH TO ZOOM";
    const CNC_IOSBuildEntry *active=nullptr;
    for(int i=0;i<_game.build_count;i++){const auto &b=_game.builds[i];if(b.is_structure&&(b.constructing||b.completed||b.on_hold)){active=&b;break;}}
    [_build setTitle:active?(active->completed?@"Building ready":active->on_hold?@"Build on hold":[NSString stringWithFormat:@"Building · %d%%",(int)(active->progress*100)]):@"Build" forState:UIControlStateNormal];
    _build.layer.borderColor=(active&&active->completed?Gold():Line()).CGColor;
    if(_game.radar_available){
        NSData *bytes=[NSData dataWithBytes:_game.radar_rgba length:sizeof(_game.radar_rgba)];CGDataProviderRef provider=CGDataProviderCreateWithCFData((__bridge CFDataRef)bytes);CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
        CGImageRef image=CGImageCreate(CNC_IOS_RADAR_SIZE,CNC_IOS_RADAR_SIZE,8,32,CNC_IOS_RADAR_SIZE*4,space,kCGImageAlphaLast|kCGBitmapByteOrderDefault,provider,NULL,NO,kCGRenderingIntentDefault);
        _radar.image=[UIImage imageWithCGImage:image];CGImageRelease(image);CGColorSpaceRelease(space);CGDataProviderRelease(provider);
    }
    CGFloat side=_radar.bounds.size.width;
    CGFloat scale=side/MAX(1,MAX(_game.map_width,_game.map_height));
    _radar.mapRect=CGRectMake((side-_game.map_width*scale)*.5,(side-_game.map_height*scale)*.5,_game.map_width*scale,_game.map_height*scale);
    UIBezierPath *view=[UIBezierPath bezierPath];for(int i=0;i<4;i++){CGPoint p=CGPointMake(_radar.mapRect.origin.x+_game.view_x[i]*_radar.mapRect.size.width,_radar.mapRect.origin.y+_game.view_y[i]*_radar.mapRect.size.height);if(!i)[view moveToPoint:p];else [view addLineToPoint:p];}[view closePath];_radar.viewport=view;
    [_radar setNeedsDisplay];[self refreshCatalog];
    CNC_IOSPlacement legal={};BOOL found=_game.placing&&cnc_ios_game_find_placement(&legal)&&[self placementVisible:legal];
    CGPoint target=CGPointZero;
    if(found&&legal.cell_count)target=[self points:CGPointMake(legal.screen_x,legal.screen_y)];
    CGPoint mobile=[self points:CGPointMake(_game.first_mobile_x,_game.first_mobile_y)];
    NSDictionary *facts=@{@"selected_count":@(_game.selected_count),@"construction_state":active?(active->completed?@"ready":active->on_hold?@"held":@"building"):@"idle",@"placed_count":@(_game.own_structure_count),@"placing":@(_game.placing),@"ghost_committed":@(_placementRequests>0),@"choice_selected":@(_choice.selected),@"placement_requests":@(_game.placement_requests_sent),@"construction_requests":@(_constructionRequests),@"placement_valid":@(found),@"placement_x":@(target.x/MAX(1.,self.bounds.size.width)),@"placement_y":@(target.y/MAX(1.,self.bounds.size.height)),@"camera_x":@(_game.camera_x),@"camera_y":@(_game.camera_y),@"compass_open":@(_compass),@"compass_choice":@(_direction),@"first_mobile_x":@(mobile.x/MAX(1.,self.bounds.size.width)),@"first_mobile_y":@(mobile.y/MAX(1.,self.bounds.size.height)),@"order_requests":@(_game.order_requests_sent),@"stop_requests":@(_game.stop_requests_sent),@"last_order_mode":@(_game.last_order_mode)};
    NSData *json=[NSJSONSerialization dataWithJSONObject:facts options:0 error:NULL];_status.accessibilityValue=[[NSString alloc]initWithData:json encoding:NSUTF8StringEncoding];
    self.hidden=_game.modal;[self setNeedsDisplay];
}
- (void)drawRect:(CGRect)rect {
    Plate(_sidebarRect,Panel(),Line());Plate(_deckRect,Panel(),Line());
    Text(@"C&C · FIELD",CGRectInset(CGRectMake(_sidebarRect.origin.x,_sidebarRect.origin.y,_sidebarRect.size.width,22),8,4),11,Gold());
    CGFloat y=CGRectGetMaxY(_radar.frame)+8;
    Text([NSString stringWithFormat:@"$%d",_game.credits],CGRectMake(_sidebarRect.origin.x+10,y,_sidebarRect.size.width-20,25),18,Gold());
    Text([NSString stringWithFormat:@"POWER %d / %d",_game.power_drained,_game.power_produced],CGRectMake(_sidebarRect.origin.x+10,y+25,_sidebarRect.size.width-20,15),11,Ink());
    if(_place.active&&_place.cell_count){
        CGContextRef ctx=UIGraphicsGetCurrentContext();
        for(int i=0;i<_place.cell_count;i++){
            UIBezierPath *p=[UIBezierPath bezierPath];
            for(int j=0;j<4;j++){CGPoint v=[self points:CGPointMake(_place.cells[i].screen_x[j],_place.cells[i].screen_y[j])];if(!j)[p moveToPoint:v];else [p addLineToPoint:v];}[p closePath];
            UIColor *c=_place.legal?[UIColor colorWithRed:.65 green:.91 blue:.40 alpha:.30]:[UIColor colorWithRed:1 green:.28 blue:.18 alpha:.30];
            CGContextSaveGState(ctx);CGContextClipToRect(ctx,_fieldRect);[c setFill];[p fill];[(_place.legal?Gold():UIColor.systemRedColor)setStroke];p.lineWidth=1.5;[p stroke];CGContextRestoreGState(ctx);
        }
        if(_choice.selected){
            CGRect footprint=CGRectNull;
            for(int i=0;i<_place.cell_count;i++)for(int j=0;j<4;j++){
                CGPoint p=[self points:CGPointMake(_place.cells[i].screen_x[j],_place.cells[i].screen_y[j])];
                footprint=CGRectUnion(footprint,CGRectMake(p.x,p.y,.01,.01));
            }
            CGFloat bw=MIN(260.,_fieldRect.size.width-12),x=MAX(CGRectGetMinX(_fieldRect)+6,MIN(CGRectGetMaxX(_fieldRect)-bw-6,_lastTap.x-bw/2));
            CGRect safe=CGRectInset(_fieldRect,6,6);safe.origin.y+=28;safe.size.height-=28;
            CGRect options[4]={CGRectMake(x,CGRectGetMinY(footprint)-46,bw,38),CGRectMake(x,CGRectGetMaxY(footprint)+8,bw,38),CGRectMake(CGRectGetMaxX(footprint)+8,_lastTap.y-19,bw,38),CGRectMake(CGRectGetMinX(footprint)-bw-8,_lastTap.y-19,bw,38)};
            for(CGRect bubble:options){
                if(!CGRectContainsRect(safe,bubble)||CGRectIntersectsRect(footprint,bubble))continue;
                Plate(bubble,Panel(),Gold());Text(@"TAP AGAIN TO BUILD HERE",CGRectInset(bubble,8,12),11,Gold(),NSTextAlignmentCenter);break;
            }
        }
    }
    if(_selecting){
        if(_pan.state==UIGestureRecognizerStateChanged){CGRect box=CGRectMake(MIN(_commandTarget.x,_lastTap.x),MIN(_commandTarget.y,_lastTap.y),fabs(_lastTap.x-_commandTarget.x),fabs(_lastTap.y-_commandTarget.y));Plate(box,[Gold()colorWithAlphaComponent:.12],Gold(),0);}
    }
    if(_compass){
        const CGPoint offsets[4]={{0,-70},{76,0},{0,70},{-76,0}};NSArray *titles=@[@"ADVANCE",@"FOCUS",@"STOP",@"MOVE"];
        Plate(CGRectMake(_compassVisual.x-112,_compassVisual.y-105,224,210),[Panel()colorWithAlphaComponent:.97],Gold(),18);
        for(int i=0;i<4;i++){
            CGRect b=CGRectMake(_compassVisual.x+offsets[i].x-35,_compassVisual.y+offsets[i].y-22,70,44);
            Plate(b,i==_direction?Gold():Panel(),Line());Text(titles[i],CGRectInset(b,2,15),11,i==_direction?Panel():Ink(),NSTextAlignmentCenter);
        }
        Text(@"CANCEL",CGRectMake(_compassVisual.x-27,_compassVisual.y-6,54,15),11,Ink(),NSTextAlignmentCenter);
    }else if(_noticeUntil>CACurrentMediaTime()&&!_choice.selected){
        CGFloat bw=MIN(360.,_fieldRect.size.width-16);CGRect b=CGRectMake(_fieldRect.origin.x+(_fieldRect.size.width-bw)/2,CGRectGetMaxY(_fieldRect)-48,bw,38);
        Plate(b,Panel(),Line());Text(_notice,CGRectInset(b,8,12),10,Ink(),NSTextAlignmentCenter);
    }
}
@end

static CNCPhoneControls *controls;
extern "C" void cnc_ios_controls_install(SDL_Window *window){
    SDL_SysWMinfo info;SDL_VERSION(&info.version);
    if(!SDL_GetWindowWMInfo(window,&info)||info.subsystem!=SDL_SYSWM_UIKIT)return;
    UIWindow *native=info.info.uikit.window;UIView *host=native.rootViewController.view;
    controls=[[CNCPhoneControls alloc]initWithWindow:window frame:host.bounds];[host addSubview:controls];
    [controls setNeedsLayout];[controls layoutIfNeeded];[controls baseTap];
    fprintf(stderr,"IOS|touch|installed|native-UIKit\n");
}
/* SDL owns a long-running native loop; drain temporary UIKit snapshots/images
   every frame rather than retaining them in SDL_main's outer pool. */
extern "C" void cnc_ios_controls_refresh(void){@autoreleasepool{[controls refresh];}}
extern "C" void cnc_ios_controls_interrupt(void){[controls interrupt];}
extern "C" void cnc_ios_controls_shutdown(void){[controls removeFromSuperview];controls=nil;}
