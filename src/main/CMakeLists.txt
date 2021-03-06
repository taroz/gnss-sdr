# Copyright (C) 2012-2013  (see AUTHORS file for a list of contributors)
#
# This file is part of GNSS-SDR.
#
# GNSS-SDR is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# at your option) any later version.
#
# GNSS-SDR is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GNSS-SDR. If not, see <http://www.gnu.org/licenses/>.
#


include_directories(
     ${CMAKE_SOURCE_DIR}/src/core/system_parameters
     ${CMAKE_SOURCE_DIR}/src/core/interfaces
     ${CMAKE_SOURCE_DIR}/src/core/receiver
     ${CMAKE_SOURCE_DIR}/src/core/libs
     ${CMAKE_SOURCE_DIR}/src/core/libs/supl
     ${CMAKE_SOURCE_DIR}/src/core/libs/supl/asn-rrlp
     ${CMAKE_SOURCE_DIR}/src/core/libs/supl/asn-supl
     ${GLOG_INCLUDE_DIRS}
     ${GFlags_INCLUDE_DIRS}
     ${ARMADILLO_INCLUDE_DIRS}  
     ${Boost_INCLUDE_DIRS}
     ${GNURADIO_RUNTIME_INCLUDE_DIRS}
)

add_definitions( -DGNSS_SDR_VERSION="${VERSION}" )

if(OS_IS_MACOSX)
     if (MACOSX_MAVERICKS)
         set(MAC_LIBRARIES "-stdlib=libc++ -std=c++11 -framework Accelerate")
     else(MACOSX_MAVERICKS)
         link_directories( /opt/local/lib )  # Not very elegant, to be fixed
         set(MAC_LIBRARIES "-lstdc++")
     endif(MACOSX_MAVERICKS)
endif(OS_IS_MACOSX)

add_executable(gnss-sdr ${CMAKE_CURRENT_SOURCE_DIR}/main.cc)

target_link_libraries(gnss-sdr ${MAC_LIBRARIES} 
                               ${Boost_LIBRARIES}
                               ${GNURADIO_RUNTIME_LIBRARIES} 
                               ${GNURADIO_BLOCKS_LIBRARIES} 
                               ${GNURADIO_FFT_LIBRARIES} 
                               ${GNURADIO_FILTER_LIBRARIES} 
                               ${GFlags_LIBS} 
                               ${GLOG_LIBRARIES} 
                               ${ARMADILLO_LIBRARIES} 
                               ${UHD_LIBRARIES}
                               gnss_sp_libs 
                               gnss_rx
                               )


install(TARGETS gnss-sdr
        DESTINATION ${CMAKE_SOURCE_DIR}/install
        )
        
       
        
