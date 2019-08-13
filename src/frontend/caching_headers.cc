/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <vector>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#include <paths.h>
#include <grp.h>
#include <cstdlib>
#include <fstream>
#include <resolv.h>
#include <sys/stat.h>
#include <dirent.h>
#include <iostream>
#include <fstream>
#include <chrono>
#include <ctime>
#include <fcntl.h> //open, O_RDONLY, O_WRONLY, O_CREAT

#include "util.hh"
#include "http_response.hh"
#include "http_record.pb.h"
#include "file_descriptor.hh"
#include "exception.hh"
#include "ezio.hh"

using namespace std;

int date_diff( const string & date1, const string & date2 )
{
    /* returns the difference, in seconds, between date1 and date2 (can be negative if date2 is later than date1) */
    tm tm_date1;
    strptime( date1.c_str(), "%a, %d %b %Y %H:%M:%S", &tm_date1 );
    auto tp_date1 = chrono::system_clock::from_time_t( std::mktime( &tm_date1 ) );
    time_t tt_date1 = chrono::system_clock::to_time_t( tp_date1 );

    tm tm_date2;
    strptime( date2.c_str(), "%a, %d %b %Y %H:%M:%S", &tm_date2 );
    auto tp_date2 = chrono::system_clock::from_time_t( mktime( &tm_date2 ) );
    time_t tt_date2 = chrono::system_clock::to_time_t( tp_date2 );

    // cerr << "date1: " << date1 << " date2: " << date2 << " diff: " << difftime( tt_date1, tt_date2) << " times: " << tt_date1 << " " << tt_date2 << endl;
    return difftime( tt_date1, tt_date2 );
    //return int(diff);
}

int get_cache_value( const string & cache_header, const string & value_to_find )
{
    /* returns value of specific field of cache header (caller has checked that value field present) */
    int value = -1;
    size_t loc = cache_header.find( value_to_find );
    if ( loc != string::npos ) { /* we have the desired value */
        loc = loc + value_to_find.size() + 1;
        size_t comma = cache_header.find( ",", loc );
        if ( comma != string::npos ) { /* we have another cache field listed after this */
            value = myatoi( cache_header.substr( loc, ( comma - loc ) ).c_str() );
        } else { /* cache header ends after this field */
            value = myatoi( cache_header.substr( loc ).c_str() );
        }
    } else {
        throw runtime_error( "get_cache_val" + value_to_find + " not present in cache header" );
    }
    return value;
}

void handle_invalid_file( const string filename )
{
    throw runtime_error( filename + " invalid HTTP request/response" );
}

int cache_control_freshness ( HTTPHeader current_header )
{
    int freshness = 0;
    // check if Cache-control header has value of no-cache or no-store...if yes, then not cacheable
    if ( HTTPMessage::equivalent_strings( current_header.key(), "Cache-control" ) ) {
        if ( (current_header.value().find("no-cache") != string::npos) ||
             (current_header.value().find("no-store") != string::npos) ) {
            return 0;
        }
        // otherwise, if Cache-control includes max-age value then its cacheable
        if ( current_header.value().find("max-age") != string::npos ) {
            int max_age_pos = current_header.value().find("max-age=");
            string remaining = current_header.value().substr(max_age_pos + 8);
            size_t fin = remaining.find(",");
            if ( fin != string::npos ) {
                freshness = stoi(remaining.substr(0, fin));
            } else {
                size_t white = remaining.find(" ");
                if ( white == string::npos ) {
                    freshness = stoi(remaining);
                } else {
                    freshness = stoi(remaining.substr(0, white));
                }
            }
            return freshness;
        }
    }
    // Cache-control header was not specified or did not include freshness amount.
    return -1;
}

void update_cacheable_headers ( MahimahiProtobufs::RequestResponse protobuf, string proto_file )
{
    MahimahiProtobufs::HTTPMessage old_response( protobuf.response() );
    MahimahiProtobufs::RequestResponse final_protobuf;
    MahimahiProtobufs::HTTPMessage final_new_response;

    final_new_response.set_first_line( protobuf.response().first_line() );

    for ( int i = 0; i < old_response.header_size(); i++ ) {
        HTTPHeader current_header( old_response.header(i) );
        if ( HTTPMessage::equivalent_strings( current_header.key(), "Expires" ) ||
            HTTPMessage::equivalent_strings( current_header.key(), "Date" ) ||
            HTTPMessage::equivalent_strings( current_header.key(), "Last-Modified" ) ||
            HTTPMessage::equivalent_strings( current_header.key(), "Age" ) ) {
            continue;
        } else {
            final_new_response.add_header()->CopyFrom( current_header.toprotobuf() );
        }
    }
    HTTPHeader cache_control( "Cache-Control: public, max-age=31536000" );
    final_new_response.add_header()->CopyFrom( cache_control.toprotobuf() );

    final_new_response.set_body( protobuf.response().body() );

    /* create new request/response pair using old request and new request */
    final_protobuf.set_ip( protobuf.ip() );
    final_protobuf.set_port( protobuf.port() );
    final_protobuf.set_scheme( protobuf.scheme() );
    final_protobuf.mutable_request()->CopyFrom( protobuf.request() );
    final_protobuf.mutable_response()->CopyFrom( final_new_response );

    /* delete previous version of protobuf file */
    if( remove( proto_file.c_str() ) != 0 ) {
        throw runtime_error( "Could not remove file: " + proto_file );
    }

    FileDescriptor messages( SystemCall( "open", open( proto_file.c_str(), O_WRONLY | O_CREAT, 00600 ) ) );
    /* write new req/res protobuf (with modification) to the same file */
    if ( not final_protobuf.SerializeToFileDescriptor( messages.fd_num() ) ) {
        throw runtime_error( "rewriting new protobuf failure to serialize new request/response pair" );
    }
}

int main( int argc, char *argv[] )
{
    try {
        if ( argc < 2 ) {
            throw runtime_error( "Usage: " + string( argv[ 0 ] ) + " directory" );
        }

        string directory = argv[ 1 ];
        const vector< string > files = list_directory_contents( directory );

        // total counts for objects and cacheable objects
        int total_obj = 0;
        int total_cacheable = 0;
        // total bytes of objects and cacheable objects
        int total_bytes = 0;
        int total_cacheable_bytes = 0;

        for ( const auto filename : files ) {
            FileDescriptor fd( SystemCall( "open", open( filename.c_str(), O_RDONLY ) ) );
            MahimahiProtobufs::RequestResponse curr;
            if ( not curr.ParseFromFileDescriptor( fd.fd_num() ) ) {
                handle_invalid_file(filename);
                continue;
            }

            MahimahiProtobufs::HTTPMessage response( curr.response() );
            bool cacheable = true;
            int freshness = 0;
            string header_used = "";
            string date_sent = "";

            total_obj += 1;
            total_bytes += response.body().length();

            // get the Date value, before looking at other headers.
            for ( int v = 0; v < response.header_size(); v++ ) {
                HTTPHeader curr_header( response.header(v) );
                if ( HTTPMessage::equivalent_strings( curr_header.key(), "Date" ) ) {
                    date_sent = curr_header.value();
                }
            }

            for ( int i = 0; i < response.header_size(); i++ ) {
                HTTPHeader current_header( response.header(i) );
                // use headers to calculate freshness---priority order is Cache-control (maxage), Expires, Last-modified
                freshness = cache_control_freshness ( current_header );
                if ( freshness > 0 ) {
                    cacheable = true;
                    break;
                }
                // Pragma: no-cache means don't cache
                if ( HTTPMessage::equivalent_strings( current_header.key(), "Pragma" ) ) {
                    if ( current_header.value().find("no-cache") != string::npos ) {
                        cacheable = false;
                    }
                }
                // Expires value can imply not-cacheable (if 0)
                if ( HTTPMessage::equivalent_strings( current_header.key(), "Expires" ) ) {
                    if ( current_header.value() == "0" ) {
                        cacheable = false;
                    }
                }
                // Expires - Date is freshness (if not set by cache-control)
                if ( HTTPMessage::equivalent_strings( current_header.key(), "Expires" ) ) {
                    if ( cacheable ) {
                        header_used = "expires";
                        string exp_date = current_header.value();
                        freshness = date_diff( exp_date, date_sent );
                    }
                }
                // Last resort is 0.1(Date-Last-Modified)
                if ( HTTPMessage::equivalent_strings( current_header.key(), "Last-Modified" ) ) {
                    if ( cacheable ) {
                        string last_modified = current_header.value();
                        if ( header_used != "expires" ) {
                            if ( last_modified != "" ) {
                                header_used = "last-modified";
                                freshness = date_diff( date_sent, last_modified ) * 0.1;
                            }
                        }
                    }
                }
            }

            // if cacheable, then update the caching headers
            // if not cacheable, do nothing to the file
            if ( cacheable && freshness > 0 ) {
                total_cacheable += 1;
                total_cacheable_bytes += response.body().length();
                cout << filename << " freshness=" << freshness << " header=" << header_used << endl;
                update_cacheable_headers( curr, filename );
                cout << "updated " << filename << endl;
            }
        }

        //float ratio = total_cacheable/float(total_obj);
        //cout << "TOTAL OBJECTS: " << total_obj << " and TOTAL CACHEABLE: " << total_cacheable << " RATIO: " << ratio << endl;
    } catch ( const runtime_error & e ) {
        print_exception( e );
        return EXIT_FAILURE;
    }
}
