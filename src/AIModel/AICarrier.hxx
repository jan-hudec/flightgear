// FGAICarrier - AIShip-derived class creates an AI aircraft carrier
//
// Written by David Culp, started October 2004.
//
// Copyright (C) 2004  David P. Culp - davidculp2@comcast.net
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

#ifndef _FG_AICARRIER_HXX
#define _FG_AICARRIER_HXX

#include <string>
#include <list>

#include <simgear/compiler.h>
#include <simgear/emesary/Emesary.hxx>

using std::string;
using std::list;

#include "AIShip.hxx"

#include "AIManager.hxx"
#include "AIBase.hxx"

class FGAIManager;
class FGAICarrier;

class FGAICarrier : public FGAIShip, simgear::Emesary::IReceiver
{
public:

    FGAICarrier();
    virtual ~FGAICarrier();

    void readFromScenario(SGPropertyNode* scFileNode) override;

    void setSign(const string& );
    void setDeckAltitudeFt(const double altitude_feet);
    void setTACANChannelID(const string &);
    double getDefaultModelRadius() override { return 350.0; }

    void bind() override;
    void UpdateWind ( double dt );
    void setWind_from_east( double fps );
    void setWind_from_north( double fps );
    void setMaxLat( double deg );
    void setMinLat( double deg );
    void setMaxLong( double deg );
    void setMinLong( double deg );
    void setMPControl( bool c );
    void setAIControl( bool c );
    void TurnToLaunch();
    void TurnToRecover();
    void TurnToBase();
    void ReturnToBox();
    bool OutsideBox();

    bool init(ModelSearchOrder searchOrder) override;

    const char* getTypeString(void) const override { return "carrier"; }

    bool getParkPosition(const string& id, SGGeod& geodPos,
                         double& hdng, SGVec3d& uvw);
    
    /**
     * @brief type-safe wrapper around AIManager::getObjectFromProperty
     */
    static SGSharedPtr<FGAICarrier> findCarrierByNameOrPennant(const std::string& namePennant);    
    
    static std::pair<bool, SGGeod> initialPositionForCarrier(const std::string& namePennant);
    
    /**
     * for a given scenario node, check for carriers within, and write nodes with
     * names, pennants and initial position into the second argument.
     * This is used to support 'start on a carrier', since we can quickly find
     * the corresponding scenario file to be loaded.
     */
    static void extractCarriersFromScenario(SGPropertyNode_ptr xmlNode, SGPropertyNode_ptr scenario);

    bool getFLOLSPositionHeading(SGGeod &pos, double &heading) const;

    double getFLOLFSGlidepathAngleDeg() const;
    double getDeckAltitudeFt() const { return deck_altitude_ft; }
    virtual simgear::Emesary::ReceiptStatus Receive(simgear::Emesary::INotificationPtr n) override;

private:
  /// Is sufficient to be private, stores a possible position to place an
  /// aircraft on start
  struct ParkPosition {
    ParkPosition(const ParkPosition& pp)
      : name(pp.name), offset(pp.offset), heading_deg(pp.heading_deg)
    {}
    ParkPosition(const string& n, const SGVec3d& off = SGVec3d(), double heading = 0)
      : name(n), offset(off), heading_deg(heading)
    {}
    string name;
    SGVec3d offset;
    double heading_deg;
  };

    void update(double dt) override;
    double wind_from_east;  // fps
    double wind_from_north; // fps
    double rel_wind_speed_kts;
    double rel_wind_from_deg;

    list<ParkPosition> ppositions;    // List of positions where an aircraft can start.
    string sign;                      // The sign of this carrier.

   // these describe the flols
    SGVec3d _flolsPosOffset, _flolsTouchdownPosition, _towerPosition, _lsoPosition;
    double _flolsHeadingOffsetDeg = 0.0; ///< angle in degrees offset from the carrier centerline
    double _flolsApproachAngle = 3.0; ///< glidepath angle for the FLOLS

    double dist;            // the distance of the eyepoint from the flols
    double angle;
    double deck_altitude_ft;
    double lineup; // lineup angle deviation from carrier;
    int source;             // the flols light which is visible at the moment
    bool in_to_wind;

    // when waveoff should be requested.
    bool   wave_off_lights_demand;

    // these are for maneuvering the carrier
    SGGeod mOpBoxPos;

    double wind_speed_kts;  //true wind speed
    double wind_from_deg;   //true wind direction
    double rel_wind;
    double max_lat, min_lat, max_long, min_long;
    double base_course, base_speed;
    double angled_deck_degrees; // angled deck offset from carrier heading. usually negative

    bool turn_to_launch_hdg;
    bool turn_to_recovery_hdg;
    bool turn_to_base_course;
    bool returning;      // set if the carrier is returning to an operating box
    bool InToWind();     // set if the carrier is in to wind
    bool MPControl, AIControl;

    int view_index;

    SGPropertyNode_ptr _longitude_node;
    SGPropertyNode_ptr _latitude_node;
    SGPropertyNode_ptr _altitude_node;
    SGPropertyNode_ptr _surface_wind_from_deg_node;
    SGPropertyNode_ptr _surface_wind_speed_node;
    SGPropertyNode_ptr _launchbar_state_node;

    SGPropertyNode_ptr _flols_x_node;
    SGPropertyNode_ptr _flols_y_node;
    SGPropertyNode_ptr _flols_z_node;
    // this is for tacan

    string TACAN_channel_id;

    SGPropertyNode_ptr _view_position_lat_deg_node;
    SGPropertyNode_ptr _view_position_lon_deg_node;
    SGPropertyNode_ptr _view_position_alt_ft_node;

    // these are for moving the elevators
    void UpdateElevator( double dt, double transition_time);
    double pos_norm, raw_pos_norm;
    double transition_time, time_constant;
    bool elevators;

    // these are for moving the jet blast deflectors
    void UpdateJBD( double dt, double jbd_transition_time);
    double jbd_pos_norm, raw_jbd_pos_norm;
    double jbd_transition_time, jbd_time_constant;
    bool jbd;
};

#endif  // _FG_AICARRIER_HXX
