/**
 * Copyright (c) 2017 Melown Technologies SE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * *  Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "../../Map.h"
#include <vts-browser/options.hpp>

#import "OptionsViewController.h"


@interface OptionsViewController ()

@property (weak, nonatomic) IBOutlet UISegmentedControl *optTraversal;
@property (weak, nonatomic) IBOutlet UISlider *optQualityDegrad;
@property (weak, nonatomic) IBOutlet UISegmentedControl *optControlType;
@property (weak, nonatomic) IBOutlet UISlider *optTouchSize;
@property (weak, nonatomic) IBOutlet UISwitch *optTouchAreas;
@property (weak, nonatomic) IBOutlet UISwitch *optControlScales;
@property (weak, nonatomic) IBOutlet UISwitch *optControlCompas;

@end


namespace
{
    NSURL *configStorePath()
    {
        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        return [[NSURL fileURLWithPath:[paths objectAtIndex:0]] URLByAppendingPathComponent:@"configApp.plist"];
    }

    void loadConfig()
    {
        NSURL *path = configStorePath();
        NSData *data = [NSData dataWithContentsOfURL:path];
        NSKeyedUnarchiver *coder = [[NSKeyedUnarchiver alloc] initForReadingWithData:data];

        if ([coder containsValueForKey:@"traverseMode"])
            map->options().traverseModeSurfaces = (vts::TraverseMode)[coder decodeIntForKey:@"traverseMode"];
        if ([coder containsValueForKey:@"maxTexelToPixelScale"])
            map->options().maxTexelToPixelScale = [coder decodeDoubleForKey:@"maxTexelToPixelScale"];
        if ([coder containsValueForKey:@"controlType"])
            extraConfig.controlType = [coder decodeIntForKey:@"controlType"];
        if ([coder containsValueForKey:@"touchSize"])
            extraConfig.touchSize = [coder decodeFloatForKey:@"touchSize"];
        if ([coder containsValueForKey:@"showControlAreas"])
            extraConfig.showControlAreas = [coder decodeBoolForKey:@"showControlAreas"];
        if ([coder containsValueForKey:@"showControlScales"])
            extraConfig.showControlScales = [coder decodeBoolForKey:@"showControlScales"];
        if ([coder containsValueForKey:@"showControlCompas"])
            extraConfig.showControlCompas = [coder decodeBoolForKey:@"showControlCompas"];

        [coder finishDecoding];
    }

    void saveConfig()
    {
        NSMutableData *data = [NSMutableData data];
        NSKeyedArchiver *coder = [[NSKeyedArchiver alloc] initForWritingWithMutableData:data];

        [coder encodeInt:(int)map->options().traverseModeSurfaces forKey:@"traverseMode"];
        [coder encodeDouble:map->options().maxTexelToPixelScale forKey:@"maxTexelToPixelScale"];
        [coder encodeInt:extraConfig.controlType forKey:@"controlType"];
        [coder encodeFloat:extraConfig.touchSize forKey:@"touchSize"];
        [coder encodeBool:extraConfig.showControlAreas forKey:@"showControlAreas"];
        [coder encodeBool:extraConfig.showControlScales forKey:@"showControlScales"];
        [coder encodeBool:extraConfig.showControlCompas forKey:@"showControlCompas"];

        [coder finishEncoding];
        NSURL *path = configStorePath();
        [data writeToURL:path atomically:YES];
    }
}

void loadAppConfig()
{
    loadConfig();
}


@implementation OptionsViewController

- (void)updateShowControls
{
    extraConfig.showControlScales = extraConfig.controlType == 0;
    extraConfig.showControlCompas = extraConfig.controlType == 1;
}

- (void)updateViewControls
{
    // rendering
    _optTraversal.selectedSegmentIndex = (int)map->options().traverseModeSurfaces;
    _optQualityDegrad.value = map->options().maxTexelToPixelScale;
    // controls
    _optTouchSize.enabled = extraConfig.controlType == 0;
    _optTouchAreas.enabled = extraConfig.controlType == 0;
    if (!_optTouchAreas.enabled)
        extraConfig.showControlAreas = false;
    _optTouchSize.value = extraConfig.touchSize;
    _optTouchAreas.on = extraConfig.showControlAreas;
    _optControlScales.on = extraConfig.showControlScales;
    _optControlCompas.on = extraConfig.showControlCompas;
    _optControlType.selectedSegmentIndex = extraConfig.controlType;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    [self updateViewControls];
}

- (IBAction)optTraversalChanged:(UISegmentedControl *)sender
{
    map->options().traverseModeSurfaces = (vts::TraverseMode)sender.selectedSegmentIndex;
    saveConfig();
}

- (IBAction)optQualityDegradChanged:(UISlider *)sender
{
    map->options().maxTexelToPixelScale = sender.value;
    saveConfig();
}

- (IBAction)optControlTypeChanged:(UISegmentedControl *)sender
{
    extraConfig.controlType = (int)sender.selectedSegmentIndex;
    [self updateShowControls];
    [self updateViewControls];
    saveConfig();
}

- (IBAction)optTouchSizeChanged:(UISlider *)sender
{
    extraConfig.touchSize = sender.value;
    saveConfig();
}

- (IBAction)optTouchAreasChanged:(UISwitch *)sender
{
    extraConfig.showControlAreas = sender.on;
    saveConfig();
}

- (IBAction)optControlScalesChanged:(UISwitch *)sender
{
    extraConfig.showControlScales = sender.on;
    [self updateViewControls];
    saveConfig();
}

- (IBAction)optControlCompasChanged:(UISwitch *)sender
{
    extraConfig.showControlCompas = sender.on;
    [self updateViewControls];
    saveConfig();
}

- (IBAction)resetAllToDefaults:(UIButton *)sender
{
    extraConfig = ExtraConfig();
    map->options() = defaultMapOptions();
    [self updateViewControls];
    saveConfig();
}


@end

