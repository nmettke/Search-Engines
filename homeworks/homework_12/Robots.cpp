// Robots.cpp
//
// Nicole Hamilton, nham@umich.edu

// Simple wrapper to exercise the RobotsTxt class.
// Works under either Windows or Linux.

#include <iostream>
#include <fstream>
#include <string.h>
#include "RobotsTxt.h"

using namespace std;

char *ReadFile( char *filename, size_t &fileSize )
   {
   // Read the file into memory.

   // Attempt to Create an istream and seek to the end
   // to get the size.
   ifstream ifs( filename, ios::ate | ios::binary );
   if ( !ifs.is_open( ) )
      return nullptr;
   fileSize = ifs.tellg( );

   // Allocate a block of memory big enough to hold it.
   char *buffer = new char[ fileSize ];

   // Seek back to the beginning of the file, read it into
   // the buffer, then return the buffer.
   ifs.seekg( 0 );
   ifs.read( buffer, fileSize );
   return buffer;
   }


void Pause( );

int main( int argc, char **argv )
   {
   // Pause( );  // Pause to allow attaching with a debugger.

   if ( argc != 2 && ( argc != 3 || strcmp( argv[ 1 ], "-p" ) ) )
      {
      cout <<  "Usage:  Robots [ -p ] filename\n"
               "\n"
               "Compile the specified file as a robots.txt file, then\n"
               "compare read user and url pairs read from stdin against\n"
               "it, reporting whether access is allowed and any crawl\n"
               "delay.\n"
               "\n"
               "   -p  Check paths, not full URLs.\n"
               "\n";
      return 1;
      }

   bool checkpath = argc == 3;


   size_t fileSize = 0;
   Utf8 *buffer = ( Utf8 * ) ReadFile( argv[ 1 + ( argc == 3 ) ], fileSize );
   if ( !buffer )
      {
      cerr << "Could not open the file " << argv[ 1 ] << "." << endl;
      return 4;
      }
   RobotsTxt robots( buffer, fileSize );

   vector< string > sitemap = robots.Sitemap( );

   if ( sitemap.size( ) )
      {
      cout << "The following sitemaps were found." << endl;
      for ( auto s : sitemap )
         cout << s << endl;
      }
   else
      cout << "No sitemaps were found." << endl;

   string user, path;

   while ( cin >> user >> path )
      {
      int crawlDelay;
      Utf8 *u, *p;

      cout << "Checking user = " << user << ", URL = " << path << endl;

      u = ( Utf8 * )user.c_str( );
      p = ( Utf8 *)path.c_str( );
      bool accessAllowed = checkpath ? robots.PathAllowed( u, p, &crawlDelay ) :
         robots.UrlAllowed( u, p, &crawlDelay );

      if ( accessAllowed )
         if ( crawlDelay )
            cout <<  "Access allowed, crawl delay = " << crawlDelay << endl;
         else
            cout <<  "Access allowed" << endl;
      else
         cout << "Access denied" << endl;
      }

   delete [ ] buffer;
   return 0;
   }
