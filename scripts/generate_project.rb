#!/usr/bin/env ruby
# frozen_string_literal: true
#
# generate_project.rb — Génère SDRDriverHub.xcodeproj (app iPad + dext RTL-SDR).
#
#   GEM_HOME=/opt/homebrew/Cellar/cocoapods/1.16.2_1/libexec ruby scripts/generate_project.rb
#
# EXPERIMENTAL — DEVELOPMENT ONLY. Reprend la cible DriverKit du générateur du
# prototype HackRF de Belksiri Spectrum (poc/ipados_hackrf/scripts/generate_project.rb).

require 'xcodeproj'
require 'fileutils'

ROOT = File.expand_path('..', __dir__)
PROJECT_PATH = File.join(ROOT, 'SDRDriverHub.xcodeproj')
TEAM_ID = ENV.fetch('HUB_TEAM_ID', 'WVQH467NC8')
APP_ID = 'com.belksiri.sdrdriverhub'
DEXT_ID = "#{APP_ID}.rtlsdr-driver"
BUILD_ID = Time.now.strftime('%m%d%H%M').to_i
DRIVERKIT_DEPLOYMENT_TARGET = '21.0'

FileUtils.rm_rf(PROJECT_PATH)
project = Xcodeproj::Project.new(PROJECT_PATH)
project.build_configurations.each do |c|
  c.build_settings.merge!('ALWAYS_SEARCH_USER_PATHS' => 'NO', 'CLANG_ENABLE_OBJC_ARC' => 'YES',
                          'SWIFT_VERSION' => '5.0', 'DEVELOPMENT_TEAM' => TEAM_ID,
                          'ENABLE_USER_SCRIPT_SANDBOXING' => 'NO')
  debug = c.name == 'Debug'
  c.build_settings['GCC_OPTIMIZATION_LEVEL'] = debug ? '0' : 's'
  c.build_settings['SWIFT_OPTIMIZATION_LEVEL'] = debug ? '-Onone' : '-O'
end

def group(project, path)
  project.main_group.find_subpath(path, true).tap { |g| g.set_source_tree('SOURCE_ROOT') }
end

# ── 1. Le dext (cible DriverKit assemblée à la main, comme dans le prototype)
dext = project.new(Xcodeproj::Project::Object::PBXNativeTarget)
project.targets << dext
dext.name = dext.product_name = 'BelksiriRtlSdrDriver'
dext.product_type = 'com.apple.product-type.driver-extension'
[Xcodeproj::Project::Object::PBXSourcesBuildPhase, Xcodeproj::Project::Object::PBXFrameworksBuildPhase,
 Xcodeproj::Project::Object::PBXResourcesBuildPhase].each { |k| dext.build_phases << project.new(k) }
list = project.new(Xcodeproj::Project::Object::XCConfigurationList)
list.default_configuration_name = 'Release'
%w[Debug Release].each do |n|
  c = project.new(Xcodeproj::Project::Object::XCBuildConfiguration)
  c.name = n
  c.build_settings = {}
  list.build_configurations << c
end
dext.build_configuration_list = list
prod = project.products_group.new_reference('BelksiriRtlSdrDriver.dext', :built_products)
prod.explicit_file_type = 'wrapper.driver-extension'
prod.include_in_index = '0'
dext.product_reference = prod

dg = group(project, 'Driver')
%w[BelksiriRtlSdrDriver.iig BelksiriRtlSdrUserClient.iig BelksiriRtlSdrDriver.cpp BelksiriRtlSdrUserClient.cpp
   bhrf_ring_geometry.c bhrf_requests.c bhrf_session.c brtl_policy.c brtl_identity.c].each do |n|
  r = dg.new_file("Driver/#{n}")
  r.explicit_file_type = 'sourcecode.iig' if n.end_with?('.iig')
  dext.source_build_phase.add_file_reference(r)
end
dext.build_configurations.each do |c|
  c.build_settings.merge!(
    'PRODUCT_NAME' => '$(TARGET_NAME)', 'PRODUCT_BUNDLE_IDENTIFIER' => DEXT_ID,
    'INFOPLIST_FILE' => 'Driver/Info.plist', 'CODE_SIGN_ENTITLEMENTS' => 'Driver/BelksiriRtlSdrDriver.entitlements',
    'SDKROOT' => 'driverkit', 'ARCHS' => 'arm64', 'DRIVERKIT_DEPLOYMENT_TARGET' => DRIVERKIT_DEPLOYMENT_TARGET,
    'CLANG_CXX_LANGUAGE_STANDARD' => 'gnu++20', 'CLANG_CXX_LIBRARY' => 'libc++', 'SKIP_INSTALL' => 'YES',
    'CODE_SIGN_STYLE' => 'Automatic', 'DEVELOPMENT_TEAM' => TEAM_ID,
    'BRTL_BUILD_ID' => BUILD_ID.to_s,
    'GCC_PREPROCESSOR_DEFINITIONS' => ['$(inherited)', 'BRTL_BUILD_ID=$(BRTL_BUILD_ID)'],
    'OTHER_LDFLAGS' => ['$(inherited)', '-framework', 'DriverKit', '-framework', 'USBDriverKit'],
    'HEADER_SEARCH_PATHS' => ['$(inherited)', '$(SRCROOT)/Driver']
  )
end

# ── 2. L'app iPad
app = project.new_target(:application, 'SDRDriverHub', :ios, '16.0')
quiet = []
refs = []
hg = group(project, 'Hub')
%w[HubApp.swift HubModel.swift RtlDevice.swift RtlTcpServer.swift RtlControlBridge.swift
   BackgroundKeepAlive.swift HubLog.swift].each { |n| refs << hg.new_file("Hub/#{n}") }
%w[Info.plist Hub.entitlements Hub-Bridging-Header.h].each { |n| hg.new_file("Hub/#{n}") }
cg = group(project, 'Core')
%w[BHRFTypes.swift HackRFTransport.swift HackRFDriverKitTransport.swift RingReader.swift RingDrain.swift
   ThroughputMeter.swift ProbeMetrics.swift].each { |n| refs << cg.new_file("Core/#{n}") }
kg = group(project, 'HubCore/Sources/HubCore')
%w[RtlTcpProtocol.swift IqContentMonitor.swift IqSendQueue.swift GainCoalescer.swift].each do |n|
  refs << kg.new_file("HubCore/Sources/HubCore/#{n}")
end
%w[bhrf_policy.c bhrf_requests.c bhrf_session.c bhrf_ring_geometry.c brtl_identity.c].each do |n|
  refs << (dg.files.find { |f| f.path.end_with?(n) } || dg.new_file("Driver/#{n}"))
end
lg = group(project, 'Controller')
%w[librtlsdr/src/librtlsdr.c librtlsdr/src/tuner_r82xx.c librtlsdr/src/tuner_e4k.c librtlsdr/src/tuner_fc0012.c
   librtlsdr/src/tuner_fc0013.c librtlsdr/src/tuner_fc2580.c shim/brtl_libusb_shim.c brtl/brtl_rtl_controller.c
   brtl/brtl_iq.c].each do |n|
  r = lg.new_file("Controller/#{n}")
  refs << r
  quiet << r
end
refs.each do |r|
  bf = app.source_build_phase.add_file_reference(r)
  bf.settings = { 'COMPILER_FLAGS' => '-w' } if quiet.include?(r)
end
app.build_configurations.each do |c|
  c.build_settings.merge!(
    'PRODUCT_NAME' => 'SDRDriverHub', 'PRODUCT_BUNDLE_IDENTIFIER' => APP_ID,
    'INFOPLIST_FILE' => 'Hub/Info.plist', 'CODE_SIGN_ENTITLEMENTS' => 'Hub/Hub.entitlements',
    'SWIFT_OBJC_BRIDGING_HEADER' => 'Hub/Hub-Bridging-Header.h',
    'HEADER_SEARCH_PATHS' => ['$(inherited)', '$(SRCROOT)/Driver', '$(SRCROOT)/Controller/shim',
                              '$(SRCROOT)/Controller/brtl', '$(SRCROOT)/Controller/librtlsdr/include'],
    'OTHER_LDFLAGS' => ['$(inherited)', '-framework', 'IOKit'],
    'IPHONEOS_DEPLOYMENT_TARGET' => '16.0', 'TARGETED_DEVICE_FAMILY' => '2',
    'CODE_SIGN_STYLE' => 'Automatic', 'DEVELOPMENT_TEAM' => TEAM_ID,
    'HUB_BUILD_ID' => BUILD_ID.to_s, 'SUPPORTS_MACCATALYST' => 'NO',
    'ASSETCATALOG_COMPILER_APPICON_NAME' => ''
  )
end
app.add_dependency(dext)
phase = app.new_copy_files_build_phase('Embed System Extensions')
phase.symbol_dst_subfolder_spec = :products_directory
phase.dst_path = '$(SYSTEM_EXTENSIONS_FOLDER_PATH)'
phase.add_file_reference(dext.product_reference).settings = { 'ATTRIBUTES' => ['RemoveHeadersOnCopy'] }

project.save
scheme = Xcodeproj::XCScheme.new
scheme.add_build_target(app)
scheme.set_launch_target(app)
scheme.save_as(PROJECT_PATH, 'SDRDriverHub', true)
puts "SDRDriverHub.xcodeproj — build #{BUILD_ID}, app #{APP_ID}, dext #{DEXT_ID}"
