/*!
 * \file rtlsdr_signal_source.cc
 * \brief Signal source for the Realtek RTL2832U USB dongle DVB-T receiver
 * (see http://sdr.osmocom.org/trac/wiki/rtl-sdr for more information)
 * \author Javier Arribas, 2012. jarribas(at)cttc.es
 *
 * -------------------------------------------------------------------------
 *
 * Copyright (C) 2010-2014  (see AUTHORS file for a list of contributors)
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

#include "rtlsdr_signal_source.h"
#include <iostream>
#include <boost/format.hpp>
#include <glog/logging.h>
#include <gnuradio/blocks/file_sink.h>
#include "configuration_interface.h"
#include "gnss_sdr_valve.h"
#include "GPS_L1_CA.h"


using google::LogMessage;


RtlsdrSignalSource::RtlsdrSignalSource(ConfigurationInterface* configuration,
        std::string role, unsigned int in_stream, unsigned int out_stream,
        boost::shared_ptr<gr::msg_queue> queue) :
                role_(role), in_stream_(in_stream), out_stream_(out_stream),
                queue_(queue)
{
    // DUMP PARAMETERS
    std::string empty = "";
    std::string default_dump_file = "./data/signal_source.dat";
    std::string default_item_type = "gr_complex";
    samples_ = configuration->property(role + ".samples", 0);
    dump_ = configuration->property(role + ".dump", false);
    dump_filename_ = configuration->property(role + ".dump_filename",
            default_dump_file);

    // RTLSDR Driver parameters
    AGC_enabled_ = configuration->property(role + ".AGC_enabled", true);
    freq_ = configuration->property(role + ".freq", GPS_L1_FREQ_HZ);
    gain_ = configuration->property(role + ".gain", (double)40.0);
    rf_gain_ = configuration->property(role + ".rf_gain", (double)40.0);
    if_gain_ = configuration->property(role + ".if_gain", (double)40.0);
    sample_rate_ = configuration->property(role + ".sampling_frequency", (double)2.0e6);
    item_type_ = configuration->property(role + ".item_type", default_item_type);

    if (item_type_.compare("short") == 0)
        {
            item_size_ = sizeof(short);
        }
    else if (item_type_.compare("gr_complex") == 0)
        {
            item_size_ = sizeof(gr_complex);
            // 1. Make the driver instance
            try
            {
                    rtlsdr_source_ = osmosdr::source::make();
            }
            catch( boost::exception & e )
            {
                    DLOG(FATAL) << "Boost exception: " << boost::diagnostic_information(e);
            }

            // 2 set sampling rate
            rtlsdr_source_->set_sample_rate(sample_rate_);
            std::cout << boost::format("Actual RX Rate: %f [SPS]...") % (rtlsdr_source_->get_sample_rate()) << std::endl ;
            LOG(INFO) << boost::format("Actual RX Rate: %f [SPS]...") % (rtlsdr_source_->get_sample_rate());

            // 3. set rx frequency
            rtlsdr_source_->set_center_freq(freq_);
            std::cout << boost::format("Actual RX Freq: %f [Hz]...") % (rtlsdr_source_->get_center_freq()) << std::endl ;
            LOG(INFO) << boost::format("Actual RX Freq: %f [Hz]...") % (rtlsdr_source_->get_center_freq());

            // TODO: Assign the remnant IF from the PLL tune error
            std::cout << boost::format("PLL Frequency tune error %f [Hz]...") % (rtlsdr_source_->get_center_freq() - freq_) ;
            LOG(INFO) <<  boost::format("PLL Frequency tune error %f [Hz]...") % (rtlsdr_source_->get_center_freq() - freq_) ;

            // 4. set rx gain
            if (this->AGC_enabled_ == true)
                {
                    rtlsdr_source_->set_gain_mode(true);
                    std::cout << "AGC enabled" << std::endl;
                    LOG(INFO) << "AGC enabled";
                }
            else
                {
                    rtlsdr_source_->set_gain_mode(false);
                    rtlsdr_source_->set_gain(gain_, 0);
                    rtlsdr_source_->set_if_gain(rf_gain_, 0);
                    rtlsdr_source_->set_bb_gain(if_gain_, 0);
                    std::cout << boost::format("Actual RX Gain: %f dB...") % rtlsdr_source_->get_gain() << std::endl;
                    LOG(INFO) << boost::format("Actual RX Gain: %f dB...") % rtlsdr_source_->get_gain();
                }
        }
    else
        {
            LOG(WARNING) << item_type_ << " unrecognized item type. Using short.";
            item_size_ = sizeof(short);
        }

    if (samples_ != 0)
        {
            DLOG(INFO) << "Send STOP signal after " << samples_ << " samples";
            valve_ = gnss_sdr_make_valve(item_size_, samples_, queue_);
            DLOG(INFO) << "valve(" << valve_->unique_id() << ")";
        }

    if (dump_)
        {
            DLOG(INFO) << "Dumping output into file " << dump_filename_;
            file_sink_ = gr::blocks::file_sink::make(item_size_, dump_filename_.c_str());
            DLOG(INFO) << "file_sink(" << file_sink_->unique_id() << ")";
        }
}



RtlsdrSignalSource::~RtlsdrSignalSource()
{}



void RtlsdrSignalSource::connect(gr::top_block_sptr top_block)
{
    if (samples_ != 0)
        {
            top_block->connect(rtlsdr_source_, 0, valve_, 0);
            DLOG(INFO) << "connected rtlsdr source to valve";
            if (dump_)
                {
                    top_block->connect(valve_, 0, file_sink_, 0);
                    DLOG(INFO) << "connected valve to file sink";
                }
        }
    else
        {
            if (dump_)
                {
                    top_block->connect(rtlsdr_source_, 0, file_sink_, 0);
                    DLOG(INFO) << "connected rtlsdr source to file sink";
                }
        }
}



void RtlsdrSignalSource::disconnect(gr::top_block_sptr top_block)
{
    if (samples_ != 0)
        {
            top_block->disconnect(rtlsdr_source_, 0, valve_, 0);
            if (dump_)
                {
                    top_block->disconnect(valve_, 0, file_sink_, 0);
                }
        }
    else
        {
            if (dump_)
                {
                    top_block->disconnect(rtlsdr_source_, 0, file_sink_, 0);
                }
        }
}



gr::basic_block_sptr RtlsdrSignalSource::get_left_block()
{
    LOG(WARNING) << "Trying to get signal source left block.";
    return gr::basic_block_sptr();
}



gr::basic_block_sptr RtlsdrSignalSource::get_right_block()
{
    if (samples_ != 0)
        {
            return valve_;
        }
    else
        {
            return rtlsdr_source_;
        }
}
