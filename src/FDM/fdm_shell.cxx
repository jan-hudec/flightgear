// fdm_shell.cxx -- encapsulate FDM implementations as well-behaved subsystems
//
// Written by James Turner, started June 2010.
//
// Copyright (C) 2010  Curtis L. Olson  - http://www.flightgear.org/~curt
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//
// $Id$

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <cassert>
#include <simgear/structure/exception.hxx>
#include <simgear/props/props_io.hxx>

#include <FDM/fdm_shell.hxx>
#include <FDM/flight.hxx>
#include <Aircraft/replay.hxx>
#include <Main/globals.hxx>
#include <Main/fg_props.hxx>
#include <Scenery/scenery.hxx>
#include "AIModel/AIManager.hxx"
#include "AIModel/AIAircraft.hxx"

// all the FDMs, since we are the factory method
#ifdef ENABLE_SP_FDM
#include <FDM/SP/ADA.hxx>
#include <FDM/SP/ACMS.hxx>
#include <FDM/SP/MagicCarpet.hxx>
#include <FDM/SP/Balloon.h>
#include <FDM/SP/AISim.hpp>
#endif
#include <FDM/ExternalNet/ExternalNet.hxx>
#include <FDM/ExternalPipe/ExternalPipe.hxx>

#ifdef ENABLE_JSBSIM
#include <FDM/JSBSim/JSBSim.hxx>
#endif

#ifdef ENABLE_LARCSIM
#include <FDM/LaRCsim/LaRCsim.hxx>
#endif

#include <FDM/UFO.hxx>
#include <FDM/NullFDM.hxx>

#ifdef ENABLE_YASIM
#include <FDM/YASim/YASim.hxx>
#endif

#include <GUI/MessageBox.hxx>
#include <Main/sentryIntegration.hxx>

using std::string;

FDMShell::FDMShell() : _tankProperties(fgGetNode("/consumables/fuel", true))
{
}

FDMShell::~FDMShell()
{
}

void FDMShell::init()
{
  _props = globals->get_props();
  fgSetBool("/sim/fdm-initialized", false);

  _wind_north       = _props->getNode("environment/wind-from-north-fps",    true);
  _wind_east        = _props->getNode("environment/wind-from-east-fps",     true);
  _wind_down        = _props->getNode("environment/wind-from-down-fps",     true);
  _control_fdm_atmo = _props->getNode("environment/params/control-fdm-atmosphere", true);
  _temp_degc        = _props->getNode("environment/temperature-degc",       true);
  _pressure_inhg    = _props->getNode("environment/pressure-inhg",          true);
  _density_slugft   = _props->getNode("environment/density-slugft3",        true);
  _data_logging     = _props->getNode("/sim/temp/fdm-data-logging",         true);
  _replay_master    = _props->getNode("/sim/freeze/replay-state",           true);

  // AI aerodynamic wake interaction
  _max_radius_nm    = _props->getNode("fdm/ai-wake/max-radius-nm",          true);
  _ai_wake_enabled  = _props->getNode("fdm/ai-wake/enabled",                true);

  _nanCheckFailed = false;
  fgSetBool("/sim/fdm-nan-failure", false);
  _lastValidPos = SGGeod::invalid();

  createImplementation();
}

void FDMShell::postinit()
{
    _initialFdmProperties = new SGPropertyNode;
    
    if (!copyProperties(_props->getNode("fdm", true),
                                     _initialFdmProperties))
    {
        SG_LOG(SG_FLIGHT, SG_ALERT, "Failed to save initial FDM property state");
    }

    _ai_mgr = globals->get_subsystem<FGAIManager>();
    if (!_ai_mgr) {
        SG_LOG(SG_FLIGHT, SG_DEV_ALERT, "FDM connection to the AI manager: FAILED");
    }
}

void FDMShell::shutdown()
{
    if (_impl) {
        fgSetBool("/sim/fdm-initialized", false);
        _impl->unbind();
        _impl.clear();
    }
    
    _props.clear();
    _wind_north.clear();
    _wind_east.clear();
    _wind_down.clear();
    _control_fdm_atmo.clear();
    _temp_degc.clear();
    _pressure_inhg.clear();
    _density_slugft .clear();
    _data_logging.clear();
    _replay_master.clear();
}

void FDMShell::reinit()
{
  shutdown();
  
    if ( copyProperties(_initialFdmProperties, fgGetNode("/fdm", true)) ) {
        SG_LOG( SG_FLIGHT, SG_INFO, "Preserved state restored successfully" );
    } else {
        SG_LOG( SG_FLIGHT, SG_WARN,
               "FDM: Some errors restoring preserved state" );
    }

    
  init();
}

void FDMShell::bind()
{
  _tankProperties.bind();
  if (_impl && _impl->get_inited()) {
    if (_impl->get_bound()) {
      throw sg_exception("FDMShell::bind of bound FGInterface impl");
    }
    _impl->bind();
  }
}

void FDMShell::unbind()
{
  if( _impl ) _impl->unbind();
  _tankProperties.unbind();
}

void FDMShell::update(double dt)
{
  if (!_impl) {
    return;
  }

  if (_nanCheckFailed) {
      return;
  }

  if (!_impl->get_inited()) {
    // Check for scenery around the aircraft.
    double lon = fgGetDouble("/sim/presets/longitude-deg");
    double lat = fgGetDouble("/sim/presets/latitude-deg");
        
    double range = 1000.0; // in meters
    SGGeod geod = SGGeod::fromDeg(lon, lat);
    const auto startUpPositionFialized = fgGetBool("/sim/position-finalized", false);
    if (startUpPositionFialized && globals->get_scenery()->scenery_available(geod, range)) {
        SG_LOG(SG_FLIGHT, SG_INFO, "Scenery loaded, will init FDM");
        _impl->init();
        if (_impl->get_bound()) {
          _impl->unbind();
        }
        _impl->bind();
        
        fgSetBool("/sim/fdm-initialized", true);
        fgSetBool("/sim/signals/fdm-initialized", true);

        if (!copyProperties(_props->getNode("fdm", true),
                                     _initialFdmProperties))
        {
            SG_LOG(SG_FLIGHT, SG_ALERT, "Failed to save initial FDM property state");
        }
    }
  }

  if (!_impl->get_inited()) {
    return; // still waiting
  }

  // AI aerodynamic wake interaction
  if (_ai_wake_enabled->getBoolValue()) {
      for (FGAIBase* base : _ai_mgr->get_ai_list()) {
          try {
              if (base->isa(FGAIBase::otAircraft) ) {
                  SGVec3d pos = _impl->getCartPosition();
                  const SGSharedPtr<FGAIAircraft> aircraft = dynamic_cast<FGAIAircraft*>(base);
                  double range = _ai_mgr->calcRangeFt(pos, aircraft)*SG_FEET_TO_METER;

                  if (!aircraft->onGround() && aircraft->getSpeed() > 0.0
                      && range < _max_radius_nm->getDoubleValue()*SG_NM_TO_METER) {
                      _impl->add_ai_wake(aircraft);
                  }
              }
          } catch (sg_exception& e) {
              SG_LOG(SG_FLIGHT, SG_WARN, "caught exception updating AI model:"
                     << base->_getName()<< ", which will be killed."
                     "\n\tError:" << e.getFormattedMessage());
              base->setDie(true);
          }
      }
  }

  // pull environmental data in, since the FDMs are lazy
  _impl->set_Velocities_Local_Airmass(
          _wind_north->getDoubleValue(),
          _wind_east->getDoubleValue(),
          _wind_down->getDoubleValue());

  if (_control_fdm_atmo->getBoolValue()) {
    // convert from Rankine to Celsius
    double tempDegC = _temp_degc->getDoubleValue();
    _impl->set_Static_temperature((9.0/5.0) * (tempDegC + 273.15));
    
    // convert from inHG to PSF
    double pressureInHg = _pressure_inhg->getDoubleValue();
    _impl->set_Static_pressure(pressureInHg * 70.726566);
    // keep in slugs/ft^3
    _impl->set_Density(_density_slugft->getDoubleValue());
  }

  bool doLog = _data_logging->getBoolValue();
  if (doLog != _dataLogging) {
    _dataLogging = doLog;
    _impl->ToggleDataLogging(doLog);
  }

  switch(_replay_master->getIntValue())
  {
      case 0:
          // normal FDM operation
          _impl->update(dt);
          break;
      case 3:
          // resume FDM operation at current replay position
          _impl->reinit();
          break;
      default:
          // replay is active
          break;
  }

  validateOutputProperties();
}

FGInterface* FDMShell::getInterface() const
{
    return _impl;
}

void FDMShell::createImplementation()
{
  assert(!_impl);
  
  double dt = 1.0 / fgGetInt("/sim/model-hz");
  string model = fgGetString("/sim/flight-model");

  bool fdmUnavailable = false;

  if ( model == "ufo" ) {
    _impl = new FGUFO( dt );
  } else if ( model == "external" ) {
    // external is a synonym for "--fdm=null" and is
    // maintained here for backwards compatibility
    _impl = new FGNullFDM( dt );
  } else if ( model.find("network") == 0 ) {
    string host = "localhost";
    int port1 = 5501;
    int port2 = 5502;
    int port3 = 5503;
    string net_options = model.substr(8);
    string::size_type begin, end;
    begin = 0;
    // host
    end = net_options.find( ",", begin );
    if ( end != string::npos ) {
      host = net_options.substr(begin, end - begin);
      begin = end + 1;
    }
    // port1
    end = net_options.find( ",", begin );
    if ( end != string::npos ) {
      port1 = atoi( net_options.substr(begin, end - begin).c_str() );
      begin = end + 1;
    }
    // port2
    end = net_options.find( ",", begin );
    if ( end != string::npos ) {
      port2 = atoi( net_options.substr(begin, end - begin).c_str() );
      begin = end + 1;
    }
    // port3
    end = net_options.find( ",", begin );
    if ( end != string::npos ) {
      port3 = atoi( net_options.substr(begin, end - begin).c_str() );
      begin = end + 1;
    }
    _impl = new FGExternalNet( dt, host, port1, port2, port3 );
  } else if ( model.find("pipe") == 0 ) {
    // /* old */ string pipe_path = model.substr(5);
    // /* old */ _impl = new FGExternalPipe( dt, pipe_path );
    string pipe_path = "";
    string pipe_protocol = "";
    string pipe_options = model.substr(5);
    string::size_type begin, end;
    begin = 0;
    // pipe file path
    end = pipe_options.find( ",", begin );
    if ( end != string::npos ) {
      pipe_path = pipe_options.substr(begin, end - begin);
      begin = end + 1;
    }
    // protocol (last option)
    pipe_protocol = pipe_options.substr(begin);
    _impl = new FGExternalPipe( dt, pipe_path, pipe_protocol );
  } else if ( model == "null" ) {
    _impl = new FGNullFDM( dt );
  }
    else if ( model == "larcsim" ) {
#ifdef ENABLE_LARCSIM
        _impl = new FGLaRCsim( dt );
#else
        fdmUnavailable = true;
#endif
    }
    else if ( model == "jsb" ) {
#ifdef ENABLE_JSBSIM
        _impl = new FGJSBsim( dt );
#else
        fdmUnavailable = true;
#endif
    }
#ifdef ENABLE_SP_FDM
    else if ( model == "ada" ) {
        _impl = new FGADA( dt );
    } else if ( model == "acms" ) {
        _impl = new FGACMS( dt );
    } else if ( model == "balloon" ) {
        _impl = new FGBalloonSim( dt );
    } else if ( model == "magic" ) {
        _impl = new FGMagicCarpet( dt );
    } else if ( model == "aisim" ) {
        _impl = new FGAISim( dt );
    }
#else
    else if (( model == "ada" )||(model == "acms")||( model == "balloon" )||( model == "magic" )||( model == "aisim" ))
    {
        fdmUnavailable = true;
    }
#endif
    else if ( model == "yasim" ) {
#ifdef ENABLE_YASIM
        _impl = new YASim( dt );
#else
        fdmUnavailable = true;
#endif
    } else {
        throw sg_exception(string("Unrecognized flight model '") + model
               + "', cannot init flight dynamics model.");
    }

    if (fdmUnavailable)
    {
        // FDM type is known, but its support was disabled at compile-time.
        throw sg_exception(string("Support for flight model '") + model
                + ("' is not available with this binary (deprecated/disabled).\n"
                   "If you still need it, please rebuild FlightGear and enable its support."));
    }
}

void FDMShell::validateOutputProperties()
{
    if (_nanCheckFailed)
        return;

    const auto p = globals->get_aircraft_position();
    if (SGMisc<double>::isNaN(p.getLatitudeDeg()) ||
        SGMisc<double>::isNaN(p.getLongitudeDeg()) ||
        SGMisc<double>::isNaN(p.getElevationFt())) {
        SG_LOG(SG_FLIGHT, SG_ALERT, "FDM position became invalid. Last valid position was:" << _lastValidPos);
        _nanCheckFailed = true;
    } else {
        _lastValidPos = p;
    }

    if (SGMisc<double>::isNaN(_impl->get_V_true_kts())) {
        SG_LOG(SG_FLIGHT, SG_ALERT, "FDM velocity became invalid");
        _nanCheckFailed = true;
    }

    if (_nanCheckFailed) {
        flightgear::sentryReportException("FDM NaN check failed");

        flightgear::modalMessageBox("Flight dynamics error",
                                    "The flight dynamics model (FDM) has become invalid. The simulation will be stopped, so you can restart at a new location.");
        fgSetBool("/sim/fdm-nan-failure", true);
        fgSetBool("/sim/freeze/master", true);
        fgSetBool("/sim/freeze/clock", true);
    }
}

// Register the subsystem.
SGSubsystemMgr::Registrant<FDMShell> registrantFDMShell(
    SGSubsystemMgr::FDM);
