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
        if ( argc < 3 ) {
            throw runtime_error( "Usage" + string( argv[ 0 ] ) + " json_file replayshell_file" );
        }

        string json_file = argv[ 1 ];
        string proto_file = argv[ 2 ];

        /* read in modified text file */
        string str, content = "";
        ifstream in;
        FileDescriptor fd( SystemCall( "open", open( json_file.c_str(), O_RDONLY ) ) );
        in.open( json_file.c_str() );
        getline( in, str );
        while ( in ) {
            content += (str + "\n");
            getline( in, str );
        }
        /* remove extra /n at the end */
        content = content.substr( 0, content.size() - 1 );
        cout << "Expected 3150: " << content.length() << endl;

        MahimahiProtobufs::RequestResponse protobuf;

        {
            FileDescriptor old( SystemCall( "open proto_file", open( proto_file.c_str(), O_RDONLY ) ) );

            /* load top-level html req/res protobuf to be modified */
            if ( not protobuf.ParseFromFileDescriptor( old.fd_num() ) ) {
                throw runtime_error( proto_file + " invalid HTTP request/response" );
            }
        }

        MahimahiProtobufs::RequestResponse final_protobuf;

        /* Handling json request message */
        MahimahiProtobufs::HTTPMessage old_request( protobuf.request() );
        MahimahiProtobufs::HTTPMessage final_new_request;

        string req_first_line = protobuf.request().first_line();
        string remove_first_space = req_first_line.substr(req_first_line.find(" ")+1);
        size_t second_space_index = remove_first_space.find(" ");
        string http_version = remove_first_space.substr(second_space_index+1);

        /* next two lines are not needed really */
        // string main_html_uri = remove_first_space.substr(0, second_space_index);
        // cerr << "Here is the uri between GET and HTTP: \"" << main_html_uri << "\"" << endl;

        string new_first_line = "GET /" + json_file + " " + http_version;
        final_new_request.set_first_line( new_first_line );
        cout << "Changed first_line from " << req_first_line << " to " << new_first_line << endl;
        final_new_request.set_body( protobuf.request().body() ); // which is empty ("")

        for ( int i = 0; i < old_request.header_size(); i++ ) {
            HTTPHeader current_header( old_request.header(i) );
            final_new_request.add_header()->CopyFrom( current_header.toprotobuf() );
        }

        /* Handling new response message */
        MahimahiProtobufs::HTTPMessage old_response( protobuf.response() );
        MahimahiProtobufs::HTTPMessage final_new_response;

        final_new_response.set_first_line( protobuf.response().first_line() );

        for ( int i = 0; i < old_response.header_size(); i++ ) {
            HTTPHeader current_header( old_response.header(i) );
            if ( HTTPMessage::equivalent_strings( current_header.key(), "Content-Encoding" ) ||
                HTTPMessage::equivalent_strings( current_header.key(), "Transfer-Encoding" ) ||
                HTTPMessage::equivalent_strings( current_header.key(), "Content-Type" ) ||
                HTTPMessage::equivalent_strings( current_header.key(), "Content-Length" ) ) {
                // cerr << "old value for Content-Type:" << current_header.value() << endl;
                continue;
            } else {
                final_new_response.add_header()->CopyFrom( current_header.toprotobuf() );
            }
        }

        HTTPHeader type_header( "Content-Type: application/json" );
        final_new_response.add_header()->CopyFrom( type_header.toprotobuf() );

        string length = to_string( content.length() );
        HTTPHeader length_header( "Content-Length: " + length );
        final_new_response.add_header()->CopyFrom( length_header.toprotobuf() );

        /* use given json file as new response body */
        final_new_response.set_body( content );

        /* create new request/response pair using old request and new request */
        final_protobuf.set_ip( protobuf.ip() );
        final_protobuf.set_port( protobuf.port() );
        final_protobuf.set_scheme( protobuf.scheme() );
        final_protobuf.mutable_request()->CopyFrom( final_new_request );
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

    } catch ( const runtime_error & e ) {
        print_exception( e );
        return EXIT_FAILURE;
    }
}
