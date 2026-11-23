#!/usr/bin/env ruby
# frozen_string_literal: true
#
# generate_project.rb — Génère SDRDriverHub.xcodeproj (app iPad + dext RTL-SDR).
#
#   GEM_HOME=/opt/homebrew/Cellar/cocoapods/1.16.2_1/libexec ruby scripts/generate_project.rb
#
# Variables :
#   HUB_DIST=1        build App Store : entitlements du pilote limités au vendeur
#                     Realtek, ET ni serveur rtl_tcp, ni format rtl_tcp, ni
#                     maintien audio (sans
#                     HUB_DIST, la condition HUB_DEV_RTLTCP les compile, et
#                     UIBackgroundModes=audio est ajouté au paquet)
#   HUB_SOURCE_URL    adresse des sources correspondantes affichée dans l'app
#                     (par défaut : la page du tag de la version, https://github.com/belksiri/sdr-driver-hub/tree/v1.0.0)
#   HUB_BUILD_NUMBER  numéro de build imposé (sinon AAAAMMJJ.HHMM, croissant)

require 'xcodeproj'
require 'fileutils'

ROOT = File.expand_path('..', __dir__)
PROJECT_PATH = File.join(ROOT, 'SDRDriverHub.xcodeproj')
TEAM_ID = ENV.fetch('HUB_TEAM_ID', 'WVQH467NC8')
APP_ID = 'com.belksiri.sdrdriverhub'
DEXT_ID = "#{APP_ID}.rtlsdr-driver"
BUILD_ID = Time.now.strftime('%m%d%H%M').to_i          # identifiant interne du dext chargé
MARKETING_VERSION = '1.0.0'
BUILD_NUMBER = ENV.fetch('HUB_BUILD_NUMBER', Time.now.strftime('%Y%m%d.%H%M'))
DIST = ENV['HUB_DIST'] == '1'
SOURCE_URL = ENV.fetch('HUB_SOURCE_URL', 'https://github.com/belksiri/sdr-driver-hub/tree/v1.0.0')
DRIVERKIT_DEPLOYMENT_TARGET = '21.0'

FileUtils.rm_rf(PROJECT_PATH)
project = Xcodeproj::Project.new(PROJECT_PATH)
project.root_object.development_region = 'en'
project.root_object.known_regions = %w[en fr Base]
project.build_configurations.each do |c|
  c.build_settings.merge!('ALWAYS_SEARCH_USER_PATHS' => 'NO', 'CLANG_ENABLE_OBJC_ARC' => 'YES',
                          'SWIFT_VERSION' => '5.0', 'DEVELOPMENT_TEAM' => TEAM_ID,
                          'ENABLE_USER_SCRIPT_SANDBOXING' => 'NO',
                          'MARKETING_VERSION' => MARKETING_VERSION, 'CURRENT_PROJECT_VERSION' => BUILD_NUMBER)
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
   bhrf_ring_geometry.c bhrf_diagnostics.c bhrf_session.c brtl_policy.c brtl_identity.c
   brtl_client_policy.c brtl_bus.c].each do |n|
  r = dg.new_file("Driver/#{n}")
  r.explicit_file_type = 'sourcecode.iig' if n.end_with?('.iig')
  dext.source_build_phase.add_file_reference(r)
end
# librtlsdr (GPL-2.0-or-later, inchangée) + shim libusb + contrôleur brtl, DANS
# le pilote (protocole client v2). DriverKit n'a ni <unistd.h>, ni
# <signal.h>, ni fprintf : Driver/dkcompat les fournit, fichier par fichier.
lg = group(project, 'Controller')
DK_FLAGS = '-w -DBRTL_DRIVERKIT -I$(SRCROOT)/Driver/dkcompat ' \
           '-include $(SRCROOT)/Driver/dkcompat/brtl_dk_prefix.h'
%w[librtlsdr/src/librtlsdr.c librtlsdr/src/tuner_r82xx.c librtlsdr/src/tuner_e4k.c librtlsdr/src/tuner_fc0012.c
   librtlsdr/src/tuner_fc0013.c librtlsdr/src/tuner_fc2580.c shim/brtl_libusb_shim.c
   brtl/brtl_rtl_controller.c].each do |n|
  r = lg.new_file("Controller/#{n}")
  dext.source_build_phase.add_file_reference(r).settings = { 'COMPILER_FLAGS' => DK_FLAGS }
end
dext.build_configurations.each do |c|
  c.build_settings.merge!(
    'PRODUCT_NAME' => '$(TARGET_NAME)', 'PRODUCT_BUNDLE_IDENTIFIER' => DEXT_ID,
    'INFOPLIST_FILE' => 'Driver/Info.plist',
    'CODE_SIGN_ENTITLEMENTS' => DIST ? 'Driver/BelksiriRtlSdrDriver.AppStore.entitlements'
                                     : 'Driver/BelksiriRtlSdrDriver.entitlements',
    'MARKETING_VERSION' => MARKETING_VERSION, 'CURRENT_PROJECT_VERSION' => BUILD_NUMBER,
    'SDKROOT' => 'driverkit', 'ARCHS' => 'arm64', 'DRIVERKIT_DEPLOYMENT_TARGET' => DRIVERKIT_DEPLOYMENT_TARGET,
    'CLANG_CXX_LANGUAGE_STANDARD' => 'gnu++20', 'CLANG_CXX_LIBRARY' => 'libc++', 'SKIP_INSTALL' => 'YES',
    'CODE_SIGN_STYLE' => 'Automatic', 'DEVELOPMENT_TEAM' => TEAM_ID,
    'BRTL_BUILD_ID' => BUILD_ID.to_s,
    'GCC_PREPROCESSOR_DEFINITIONS' => ['$(inherited)', 'BRTL_BUILD_ID=$(BRTL_BUILD_ID)',
                                       'BRTL_TEAM_ID=$(DEVELOPMENT_TEAM)'],
    'OTHER_LDFLAGS' => ['$(inherited)', '-framework', 'DriverKit', '-framework', 'USBDriverKit'],
    'HEADER_SEARCH_PATHS' => ['$(inherited)', '$(SRCROOT)/Driver', '$(SRCROOT)/Controller/shim',
                              '$(SRCROOT)/Controller/brtl', '$(SRCROOT)/Controller/librtlsdr/include']
  )
end

# ── 2. L'app iPad
# Info.plist : la source (Hub/Info.plist) n'a AUCUN mode d'arrière-plan. Les
# builds de développement reçoivent une copie dérivée qui ajoute
# UIBackgroundModes = audio pour le serveur rtl_tcp de développement.
if DIST
  APP_INFOPLIST = 'Hub/Info.plist'
else
  APP_INFOPLIST = 'build/generated/Info-dev.plist'
  dev_plist = File.join(ROOT, APP_INFOPLIST)
  FileUtils.mkdir_p(File.dirname(dev_plist))
  FileUtils.cp(File.join(ROOT, 'Hub/Info.plist'), dev_plist)
  system('/usr/libexec/PlistBuddy', '-c', 'Add :UIBackgroundModes array',
         '-c', 'Add :UIBackgroundModes:0 string audio', dev_plist, exception: true)
end

app = project.new_target(:application, 'SDRDriverHub', :ios, '16.0')
quiet = []
refs = []
hg = group(project, 'Hub')
Dir.chdir(ROOT) do
  # Toutes les sources Swift de l'app, du transport et du cœur pur (HubCore).
  # En App Store, le format réseau rtl_tcp (en-tête « RTL0 », découpeur de
  # commandes, file d'envoi) n'est pas compilé du tout : seul le serveur de
  # développement s'en sert. Le reste du code n'y est relié que sous
  # HUB_DEV_RTLTCP.
  dev_only = %w[HubCore/Sources/HubCore/RtlTcpProtocol.swift HubCore/Sources/HubCore/IqSendQueue.swift]
  (Dir['Hub/**/*.swift'] + Dir['Core/*.swift'] + Dir['HubCore/Sources/HubCore/*.swift']).sort.each do |f|
    next if DIST && dev_only.include?(f)
    refs << hg.new_file(f)
  end
end
%w[Info.plist Hub.entitlements Hub-Bridging-Header.h].each { |n| hg.new_file("Hub/#{n}") }
# L'app n'embarque PLUS librtlsdr : seulement le C pur qui décrit le contrat.
%w[bhrf_diagnostics.c bhrf_session.c bhrf_ring_geometry.c brtl_identity.c brtl_policy.c
   brtl_client_policy.c].each do |n|
  refs << (dg.files.find { |f| f.path.end_with?(n) } || dg.new_file("Driver/#{n}"))
end
refs.each do |r|
  bf = app.source_build_phase.add_file_reference(r)
  bf.settings = { 'COMPILER_FLAGS' => '-w' } if quiet.include?(r)
end
app.build_configurations.each do |c|
  c.build_settings.merge!(
    'PRODUCT_NAME' => 'SDRDriverHub', 'PRODUCT_BUNDLE_IDENTIFIER' => APP_ID,
    'INFOPLIST_FILE' => APP_INFOPLIST, 'CODE_SIGN_ENTITLEMENTS' => 'Hub/Hub.entitlements',
    'SWIFT_OBJC_BRIDGING_HEADER' => 'Hub/Hub-Bridging-Header.h',
    'HEADER_SEARCH_PATHS' => ['$(inherited)', '$(SRCROOT)/Driver'],
    'OTHER_LDFLAGS' => ['$(inherited)', '-framework', 'IOKit'],
    'IPHONEOS_DEPLOYMENT_TARGET' => '16.0', 'TARGETED_DEVICE_FAMILY' => '2',
    'CODE_SIGN_STYLE' => 'Automatic', 'DEVELOPMENT_TEAM' => TEAM_ID,
    'SUPPORTS_MACCATALYST' => 'NO', 'SUPPORTS_MAC_DESIGNED_FOR_IPHONE_IPAD' => 'NO',
    'ASSETCATALOG_COMPILER_APPICON_NAME' => 'AppIcon', 'ASSETCATALOG_COMPILER_GLOBAL_ACCENT_COLOR_NAME' => '',
    'MARKETING_VERSION' => MARKETING_VERSION, 'CURRENT_PROJECT_VERSION' => BUILD_NUMBER,
    'HUB_SOURCE_URL' => SOURCE_URL, 'INFOPLIST_PREPROCESS' => 'NO',
    'SWIFT_ACTIVE_COMPILATION_CONDITIONS' => [c.name == 'Debug' ? 'DEBUG' : nil,
                                              DIST ? nil : 'HUB_DEV_RTLTCP'].compact.join(' '),
    'DEBUG_INFORMATION_FORMAT' => 'dwarf-with-dsym'
  )
end
# Ressources : icône, manifeste de confidentialité, français, notices de licence.
rg = group(project, 'Resources')
[hg.new_file('Hub/Assets.xcassets'), hg.new_file('Hub/Resources/PrivacyInfo.xcprivacy'),
 rg.new_file('THIRD_PARTY_NOTICES.md'), rg.new_file('LICENSES/GPL-2.0-or-later.txt')].each do |r|
  app.resources_build_phase.add_file_reference(r)
end
loc = hg.new_variant_group('Localizable.strings')
loc.new_reference('Hub/Resources/fr.lproj/Localizable.strings').name = 'fr'
app.resources_build_phase.add_file_reference(loc)
# Textes de l'écran « Development » : builds de développement SEULEMENT.
unless DIST
  dev = hg.new_variant_group('Development.strings')
  dev.new_reference('Hub/Resources/fr.lproj/Development.strings').name = 'fr'
  app.resources_build_phase.add_file_reference(dev)
end
ips = hg.new_variant_group('InfoPlist.strings')
%w[en fr].each { |l| ips.new_reference("Hub/Resources/#{l}.lproj/InfoPlist.strings").name = l }
app.resources_build_phase.add_file_reference(ips)

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
puts "SDRDriverHub.xcodeproj — #{MARKETING_VERSION} (#{BUILD_NUMBER}), dext id #{BUILD_ID}, " \
     "#{DIST ? 'entitlements App Store' : 'entitlements développement'}, app #{APP_ID}, dext #{DEXT_ID}"
