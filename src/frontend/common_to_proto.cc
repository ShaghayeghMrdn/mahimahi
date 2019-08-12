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
//#include "system_runner.hh"
#include "http_response.hh"
#include "http_record.pb.h"
#include "file_descriptor.hh"
#include "exception.hh"

using namespace std;

int main( int argc, char *argv[] )
{
    try {
        if ( argc < 3 ) {
            throw runtime_error( "Usage: " + string( argv[ 0 ] ) + " common_html.br replayshell_file" );
        }

        string common_html = argv[ 1 ];
        string proto_file = argv[ 2 ];

        /* read in modified text file */
        string str, content = "";
        ifstream in;
        FileDescriptor fd( SystemCall( "open", open( common_html.c_str(), O_RDONLY ) ) );
        in.open( common_html.c_str() );
        getline( in, str );
        while ( in ) {
            content += (str + "\n");
            getline( in, str );
        }
        /* remove extra /n at the end */
        content = content.substr( 0, content.size() - 1 );
        // cout << "Content-Length = " << content.length() << endl;

        MahimahiProtobufs::RequestResponse protobuf;

        {
            FileDescriptor old( SystemCall( "open ", open( proto_file.c_str(), O_RDONLY ) ) );

            /* store previous version (before modification) of req/res protobuf */
            if ( not protobuf.ParseFromFileDescriptor( old.fd_num() ) ) {
                throw runtime_error( proto_file + " invalid HTTP request/response" );
            }
        }

        MahimahiProtobufs::RequestResponse final_protobuf;
        MahimahiProtobufs::HTTPMessage old_response( protobuf.response() );
        MahimahiProtobufs::HTTPMessage final_new_response;

        final_new_response.set_first_line( protobuf.response().first_line() );

        for ( int i = 0; i < old_response.header_size(); i++ ) {
            HTTPHeader current_header( old_response.header(i) );
            if ( HTTPMessage::equivalent_strings( current_header.key(), "Content-Encoding" ) ||
                 HTTPMessage::equivalent_strings( current_header.key(), "Transfer-Encoding" ) ||
                 HTTPMessage::equivalent_strings( current_header.key(), "Content-Security-Policy" ) ||
                 HTTPMessage::equivalent_strings( current_header.key(), "Content-Length" ) ) {
                continue;
            } else {
                final_new_response.add_header()->CopyFrom( current_header.toprotobuf() );
            }
        }

        HTTPHeader encoding_header( "Content-Encoding: br" );
        final_new_response.add_header()->CopyFrom( encoding_header.toprotobuf() );

        string length = to_string( content.length() );
        HTTPHeader length_header( "Content-Length: " + length );
        final_new_response.add_header()->CopyFrom( length_header.toprotobuf() );

        /* use common html as new response body */
        final_new_response.set_body( content );


        /* create new request/response pair using old request and new response */
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
    } catch ( const runtime_error & e ) {
        print_exception( e );
        return EXIT_FAILURE;
    }
}
