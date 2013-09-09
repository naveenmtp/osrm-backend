/*
    open source routing machine
    Copyright (C) Dennis Luxen, 2010

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU AFFERO General Public License as published by
the Free Software Foundation; either version 3 of the License, or
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
or see http://www.gnu.org/licenses/agpl.txt.
 */

#include "../Util/OSRMException.h"
#include "../Util/SimpleLogger.h"
#include "../Util/TimingUtil.h"

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/ref.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#ifdef __linux__
#include <malloc.h>
#endif
#include <algorithm>
#include <iomanip>
#include <numeric>
#include <vector>

const unsigned number_of_elements = 268435456;

struct Statistics { double min, max, med, mean, dev; };

void RunStatistics(std::vector<double> & timings_vector, Statistics & stats) {
    std::sort(timings_vector.begin(), timings_vector.end());
    stats.min = timings_vector.front();
    stats.max = timings_vector.back();
    stats.med = timings_vector[timings_vector.size()/2];
    double primary_sum =    std::accumulate(
                                timings_vector.begin(),
                                timings_vector.end(),
                                0.0
                            );
    stats.mean = primary_sum / timings_vector.size();

    double primary_sq_sum = std::inner_product( timings_vector.begin(),
                                timings_vector.end(),
                                timings_vector.begin(),
                                0.0
                            );
     stats.dev = std::sqrt(
        primary_sq_sum / timings_vector.size() - (stats.mean * stats.mean)
    );
}

int main (int argc, char * argv[]) {
    LogPolicy::GetInstance().Unmute();

    SimpleLogger().Write(logDEBUG) << "starting up engines, compiled at " <<
        __DATE__ << ", " __TIME__;

    if( 1 == argc ) {
        SimpleLogger().Write(logWARNING) <<
            "usage: " << argv[0] << " /path/on/device";
        return -1;
    }

    boost::filesystem::path test_path = boost::filesystem::path(argv[1]);
    test_path /= "osrm.tst";
    SimpleLogger().Write(logDEBUG) << "temporary file: " << test_path.string();

    try {
        //create files for testing
        if( 2 == argc) {
            //create file to test
            if( boost::filesystem::exists(test_path) ) {
                throw OSRMException("Data file already exists");
            }

            double time1, time2;
            int * random_array = new int[number_of_elements];
            std::generate (random_array, random_array+number_of_elements, std::rand);
#ifdef __APPLE__
            FILE * fd = fopen(test_path.string().c_str(), "w");
            fcntl(fileno(fd), F_NOCACHE, 1);
            fcntl(fileno(fd), F_RDAHEAD, 0);
            time1 = get_timestamp();
            write(fileno(fd), (char*)random_array, number_of_elements*sizeof(unsigned));
            time2 = get_timestamp();
            fclose(fd);
#endif
#ifdef __linux__
            int f = open(
                test_path.string().c_str(),
                O_CREAT|O_TRUNC|O_WRONLY|O_SYNC,
                S_IRWXU
            );
            time1 = get_timestamp();
            int ret = write(
                f,
                random_array,
                number_of_elements*sizeof(unsigned)
            );
            if(-1 == ret) {
                throw OSRMException("could not write random data file");
            }
            time2 = get_timestamp();
            close(f);
#endif
            delete[] random_array;
            SimpleLogger().Write(logDEBUG) <<
                "writing raw 1GB took " << (time2-time1)*1000 << "ms";
            SimpleLogger().Write() << "raw write performance: " <<
                std::setprecision(5) << std::fixed <<
                1024*1024/((time2-time1)*1000) << "MB/sec";

            SimpleLogger().Write(logDEBUG) <<
                "finished creation of random data. Flush disk cache now!";

        } else {

            //
            // Run Non-Cached I/O benchmarks
            //

            if( !boost::filesystem::exists(test_path) ) {
                throw OSRMException("data file does not exist");
            }

            double time1, time2;
            //volatiles do not get optimized
            Statistics stats;

#ifdef __APPLE__
            volatile unsigned temp_array[1024];
            volatile unsigned single_element = 0;
            char * raw_array = new char[number_of_elements*sizeof(unsigned)];
            FILE * fd = fopen(test_path.string().c_str(), "r");
            fcntl(fileno(fd), F_NOCACHE, 1);
            fcntl(fileno(fd), F_RDAHEAD, 0);
#endif
#ifdef __linux__
            char * temp_array = (char*) memalign(
                512,
                1024*sizeof(unsigned)
            );
            char * single_block = (char*) memalign(
                512,
                512
            );

            int f = open(test_path.string().c_str(), O_RDONLY|O_DIRECT|O_SYNC);
            SimpleLogger().Write(logDEBUG) << "opened, error: " << strerror(errno);
            char * raw_array = (char*) memalign(
                512,
                number_of_elements*sizeof(unsigned)
            );
#endif
            time1 = get_timestamp();
#ifdef __APPLE__
            read(fileno(fd), raw_array, number_of_elements*sizeof(unsigned));
            close(fileno(fd));
            fd = fopen(test_path.string().c_str(), "r");
#endif
#ifdef __linux__
            int ret = read(f, raw_array, number_of_elements*sizeof(unsigned));
            SimpleLogger().Write(logDEBUG) <<
                "read " << ret << " bytes, error: " << strerror(errno);
            close(f);
            f = open(test_path.string().c_str(), O_RDONLY|O_DIRECT|O_SYNC);
            SimpleLogger().Write(logDEBUG) <<
                "opened, error: " << strerror(errno);
#endif
            time2 = get_timestamp();

            SimpleLogger().Write(logDEBUG) <<
                "reading raw 1GB took " << (time2-time1)*1000 << "ms";
            SimpleLogger().Write() << "raw read performance: " <<
                std::setprecision(5) << std::fixed <<
                1024*1024/((time2-time1)*1000) << "MB/sec";

            std::vector<double> timing_results_raw_random;
            SimpleLogger().Write(logDEBUG) << "running 1000 random I/Os of 4KB";

#ifdef __APPLE__
            fseek(fd, 0, SEEK_SET);
#endif
#ifdef __linux__
            lseek(f, 0, SEEK_SET);
#endif
            //make 1000 random access, time each I/O seperately
            unsigned number_of_blocks = ((number_of_elements*sizeof(unsigned))-4096)/512;
            for(unsigned i = 0; i < 1000; ++i) {
                unsigned block_to_read = std::rand()%number_of_blocks;
                off_t current_offset = block_to_read*512;
                time1 = get_timestamp();
#ifdef __APPLE__
                int ret1 = fseek(fd, current_offset, SEEK_SET);
                int ret2 = read(fileno(fd), (char*)&temp_array[0], 1024*sizeof(unsigned));
#endif
#ifdef __linux__
                int ret1 = lseek(f, current_offset, SEEK_SET);
                int ret2 = read(f, (char*)temp_array, 1024*sizeof(unsigned));
#endif
                time2 = get_timestamp();
                if( ((off_t)-1) == ret1) {
                    SimpleLogger().Write(logWARNING)
                        << "offset: " << current_offset;
                    SimpleLogger().Write(logWARNING)
                        << "seek error " << strerror(errno);
                    throw OSRMException("seek error");
                }
                 if(-1 == ret2) {
                    SimpleLogger().Write(logWARNING)
                        << "offset: " << current_offset;
                    SimpleLogger().Write(logWARNING)
                        << "read error " << strerror(errno);
                    throw OSRMException("read error");
                }
               timing_results_raw_random.push_back((time2-time1));
            }

            // Do statistics
            SimpleLogger().Write(logDEBUG) << "running raw random I/O statistics";
            RunStatistics(timing_results_raw_random, stats);

            SimpleLogger().Write() << "raw random I/O: "  <<
                std::setprecision(5) << std::fixed <<
                "min: "  << stats.min*1000.  << "ms, " <<
                "mean: " << stats.mean*1000. << "ms, " <<
                "med: "  << stats.med*1000.  << "ms, " <<
                "max: "  << stats.max*1000.  << "ms, " <<
                "dev: "  << stats.dev*1000.  << "ms";

            std::vector<double> timing_results_raw_gapped;
#ifdef __APPLE__
            fseek(fd, 0, SEEK_SET);
#endif
#ifdef __linux__
            lseek(f, 0, SEEK_SET);
#endif

            //read every 100th block
            for(
                unsigned i = 0;
                i < number_of_blocks;
                i += 1024
            ) {
                off_t current_offset = i*512;
                time1 = get_timestamp();
    #ifdef __APPLE__
                int ret1 = fseek(fd, current_offset, SEEK_SET);
                int ret2 = read(fileno(fd), (char*)&single_element, 512);
    #endif
    #ifdef __linux__
                int ret1 = lseek(f, current_offset, SEEK_SET);

                int ret2 = read(f, (char*)single_block, 512);
    #endif
                time2 = get_timestamp();
                if( ((off_t)-1) == ret1) {
                    SimpleLogger().Write(logWARNING)
                        << "offset: " << current_offset;
                    SimpleLogger().Write(logWARNING)
                        << "seek error " << strerror(errno);
                    throw OSRMException("seek error");
                }
                 if(-1 == ret2) {
                    SimpleLogger().Write(logWARNING)
                        << "offset: " << current_offset;
                    SimpleLogger().Write(logWARNING)
                        << "read error " << strerror(errno);
                    throw OSRMException("read error");
                }
               timing_results_raw_gapped.push_back((time2-time1));
            }
    #ifdef __APPLE__
            fclose(fd);
            // free(single_element);
            free(raw_array);
            // free(temp_array);
    #endif
    #ifdef __linux__
            close(f);
    #endif
            //Do statistics
            SimpleLogger().Write(logDEBUG) << "running gapped I/O statistics";
            //print simple statistics: min, max, median, variance
            RunStatistics(timing_results_raw_gapped, stats);
            SimpleLogger().Write() << "raw gapped I/O: " <<
                std::setprecision(5) << std::fixed <<
                "min: "  << stats.min*1000.  << "ms, " <<
                "mean: " << stats.mean*1000. << "ms, " <<
                "med: "  << stats.med*1000.  << "ms, " <<
                "max: "  << stats.max*1000.  << "ms, " <<
                "dev: "  << stats.dev*1000.  << "ms";

            if( boost::filesystem::exists(test_path) ) {
                boost::filesystem::remove(test_path);
                SimpleLogger().Write(logDEBUG) << "removing temporary files";
            }
        }
    } catch ( const std::exception & e ) {
        SimpleLogger().Write(logWARNING) << "caught exception: " << e.what();
        SimpleLogger().Write(logWARNING) << "cleaning up, and exiting";
        if(boost::filesystem::exists(test_path)) {
            boost::filesystem::remove(test_path);
            SimpleLogger().Write(logWARNING) << "removing temporary files";
        }
        return -1;
    }
    return 0;
}
