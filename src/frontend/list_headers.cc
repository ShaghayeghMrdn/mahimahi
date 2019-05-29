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
#include <fcntl.h> //open, O_RDONLY, O_WRONLY, O_CREAT

#include "util.hh"
// #include "system_runner.hh"
#include "http_response.hh"
#include "http_record.pb.h"
#include "file_descriptor.hh"
#include "exception.hh"

using namespace std;

int main( int argc, char *argv[] )
{
    try {
        if ( argc < 1 ) {
            throw runtime_error( "Usage" + string( argv[ 0 ] ) + " replayshell_file" );
        }

        string proto_file = argv[ 1 ];

        MahimahiProtobufs::RequestResponse protobuf;

        {
            FileDescriptor old( SystemCall( "open ", open( proto_file.c_str(), O_RDONLY ) ) );

            /* store previous version (before modification) of req/res protobuf */
            if ( not protobuf.ParseFromFileDescriptor( old.fd_num() ) ) {
                throw runtime_error( proto_file + "invalid HTTP request/response" );
            }
        }

        // cout << "request and response headers:" << endl;
        MahimahiProtobufs::HTTPMessage request( protobuf.request() );
        MahimahiProtobufs::HTTPMessage response( protobuf.response() );

        string scheme = "http://";
        if ( protobuf.scheme() == MahimahiProtobufs::RequestResponse_Scheme_HTTPS ) {
            scheme = "https://";
        }

        string host = "";
        cout << request.first_line() << endl;
        for ( int i = 0; i < request.header_size(); i++ ) {
            HTTPHeader current_header( request.header(i) );
            if ( HTTPMessage::equivalent_strings( current_header.key(), "Host" ) ) {
                host = current_header.value();
            }
            cout << current_header.key() << ":" << current_header.value() << endl;
        }
        cout << "----------------------------------------" << endl;

        // string html_name = "";
        // string complete_name = protobuf.request().first_line();
        // string remove_first_space = complete_name.substr(complete_name.find(" ")+1);
        //html_name = remove_first_space.substr(0, remove_first_space.find(" "));
        // html_name = scheme + host + html_name;
        // cout << html_name << endl;

        cout << response.first_line() << endl;
        /* use modified text file as new response body */
        for ( int i = 0; i < response.header_size(); i++ ) {
            HTTPHeader current_header( response.header(i) );
            cout << current_header.key() << ":" << current_header.value() << endl;
        }
    } catch ( const runtime_error & e ) {
        print_exception( e );
        return EXIT_FAILURE;
    }
}
