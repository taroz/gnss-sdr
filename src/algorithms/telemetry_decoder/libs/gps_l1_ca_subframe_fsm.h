/*!
 * \file gps_l1_ca_subframe_fsm.h
 * \brief  Interface of a GPS NAV message word-to-subframe decoder state machine
 * \author Javier Arribas, 2011. jarribas(at)cttc.es
 *
 * -------------------------------------------------------------------------
 *
 * Copyright (C) 2010-2012  (see AUTHORS file for a list of contributors)
 *
 * GNSS-SDR is a software defined Global Navigation
 *          Satellite Systems receiver
 *
 * This file is part of GNSS-SDR.
 *
 * GNSS-SDR is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * at your option) any later version.
 *
 * GNSS-SDR is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNSS-SDR. If not, see <http://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------------
 */


#ifndef GNSS_SDR_GPS_L1_CA_SUBFRAME_FSM_H_
#define GNSS_SDR_GPS_L1_CA_SUBFRAME_FSM_H_

#include <boost/statechart/state_machine.hpp>
#include <boost/statechart/simple_state.hpp>
#include <boost/statechart/state.hpp>
#include <boost/statechart/transition.hpp>
#include <boost/statechart/custom_reaction.hpp>
#include <boost/mpl/list.hpp>
#include <queue>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include "concurrent_queue.h"
#include <iostream>
#include <cstring>
#include "GPS_L1_CA.h"
#include "gps_navigation_message.h"
#include "gps_ephemeris.h"
#include "gps_iono.h"
#include "gps_almanac.h"
#include "gps_utc_model.h"

namespace sc = boost::statechart;
namespace mpl = boost::mpl;

struct gps_subframe_fsm_S0;
struct gps_subframe_fsm_S1;
struct gps_subframe_fsm_S2;
struct gps_subframe_fsm_S3;
struct gps_subframe_fsm_S4;
struct gps_subframe_fsm_S5;
struct gps_subframe_fsm_S6;
struct gps_subframe_fsm_S7;
struct gps_subframe_fsm_S8;
struct gps_subframe_fsm_S9;
struct gps_subframe_fsm_S10;
struct gps_subframe_fsm_S11;


/*!
 * \brief This class implements a Finite State Machine that handles the decoding
 *  of the GPS L1 C/A NAV message
 */
class GpsL1CaSubframeFsm : public sc::state_machine< GpsL1CaSubframeFsm, gps_subframe_fsm_S0 >
{
public:
    GpsL1CaSubframeFsm(); //!< The constructor starts the Finite State Machine

    // channel and satellite info
    int i_channel_ID;              //!< Channel id
    unsigned int i_satellite_PRN;  //!< Satellite PRN number

    concurrent_queue<Gps_Ephemeris> *d_ephemeris_queue; //!< Ephemeris queue
    concurrent_queue<Gps_Iono> *d_iono_queue;           //!< Ionospheric parameters queue
    concurrent_queue<Gps_Utc_Model> *d_utc_model_queue; //!< UTC model parameters queue
    concurrent_queue<Gps_Almanac> *d_almanac_queue;     //!< Almanac queue

    Gps_Navigation_Message d_nav; //!< GPS L1 C/A navigation message object

    // GPS SV and System parameters
    Gps_Ephemeris ephemeris;  //!< Object that handles GPS ephemeris parameters
    Gps_Almanac almanac;      //!< Object that handles GPS almanac data
    Gps_Utc_Model utc_model;  //!< Object that handles UTM model parameters
    Gps_Iono iono;            //!< Object that handles ionospheric parameters

    char d_subframe[GPS_SUBFRAME_LENGTH];
    char d_GPS_frame_4bytes[GPS_WORD_LENGTH];
    double d_preamble_time_ms;

    void gps_word_to_subframe(int position); //!< inserts the word in the correct position of the subframe

    /*!
     * \brief This function decodes a NAv message subframe and pushes the information to the right queues
     */
    void gps_subframe_to_nav_msg();

    //FSM EVENTS
    void Event_gps_word_valid();    //!< FSM event: the received word is valid
    void Event_gps_word_invalid();  //!< FSM event: the received word is not valid
    void Event_gps_word_preamble(); //!< FSM event: word preamble detected
};

#endif
