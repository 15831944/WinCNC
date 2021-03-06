#include "motor.h"
#include "helper.h"
#include "LCD.h"
#include "CNC.h"

// ------------------------------------------------------------------------

Motor X( 13, 12, A5,  1 );  // 13, 12, A5, 1
Motor Y( 11, 10, A4,  1 );  // 11, 10, A4, 1
Motor Z(  3,  2, A3,  1 );  //  3,  2, A3, 1

unsigned int commandCount = 0;
int error = 0;
int reset = 3;

void Move( long x, long y, long z, long d )
{
  long s;
  long t = 0;
  long tX,tY,tZ;
  
  // No movement means dwell
  if( x==0 && y==0 && z==0 )
  {
    delay( d / 1000 );
    return;
  }
  
  // This calculates the interval between steps for each axis and returns the time
  // to wait until the first move needs to occur (-1 if no move necessary).
  tX = X.InitMove( x, d );
  tY = Y.InitMove( y, d );
  tZ = Z.InitMove( z, d ); 

  do
  {
    // Finds which of the non negative wait time is the shortest
    s = minOf3( tX, tY, tZ );
    
    // Debug stuff to check for errors
    if( s <= 0 )
    {
      DebugPulse( 7 );
    }

    // Accumulate the sleep time (ignores processing time)
    t = t + s;
    // Remove the sleep time from each axis
    tX = tX - s;
    tY = tY - s;
    tZ = tZ - s;
    
    // 50uS is the estimated processing time if the step is short, just skip over the wait
    // since we're already late
    if( s > 50 )
    {
      // Remove it from our sleep time
      s = s - 50;
      
      // Refresh the screen (if enough time) and return the time we have left
      s = LCD_UpdateTask( s );

      // When sleeping more than 16388 uS, documentation says don't use delayMicroseconds.
      if( s < 16000 )
      {
        if( s ) delayMicroseconds( s );
      }
      else 
      {
        delay( s / 1000 );
        delayMicroseconds( s % 1000 );
      }
    }

    // For each axis where the time to move has been reached, move.
    // The Move function returns the time to the next move (based on t) or
    // a negative value if the axis has eached the end position.
    if( tX == 0 ) tX = X.Move( t );
    if( tY == 0 ) tY = Y.Move( t );    
    if( tZ == 0 ) tZ = Z.Move( t );
    
  } while(( tX > 0 ) || ( tY > 0 ) || ( tZ > 0 ));

  // Check proper distance was achieved for debug purpose.
  if( X.Remain( ) != 0 || Y.Remain( ) != 0 || Z.Remain( ) != 0 )
  {
    error = 1;
    reset = 3;
    LCD_SetStatus( "ERR", 3 );
    // DebugPulse( 5 );
  }
}

void Decode( char c )
{
  static long x = 0;
  static long y = 0;
  static long z = 0;
  static long d = 0;   /* Duration in micro seconds. 0 means max speed (G0) */
  static long s = -1;  /* Spindle state : -1 = No change, 0-1 = OFF-ON */
  static long i = 0;   /* Packet index */
  static int sign = 0;
  static int SOF = 0;
  static long *pt = NULL;
  static long nextIndex = 0;
  
  if( reset == 0 )
  {   
    if( c == '@' )
    {
      if( SOF == 0 ) SOF = 1;
      else
      {
        Serial.write( 'E' );
      }
    }
    else if( SOF )
    {
      if( c == 'X' ) { pt = &x; sign = 1; }
      else if( c == 'Y' ) { pt = &y; sign = 1; }
      else if( c == 'Z' ) { pt = &z; sign = 1; }
      else if( c == 'D' ) { pt = &d; sign = 1; }
      else if( c == 'S' ) { pt = &s; sign = 1; s = 0; }
      else if( c == '-' ) { sign = -1; }
      else if( c >= '0' && c <= '9' )
      {
        if( pt ) *pt = *pt * 10 + ( c - '0' ) * sign;
        else Serial.write( 'E' );
      }    
      else if( c == '\n' )
      {
        if( s >= 0 ) digitalWrite(TOOL_ON_REPLAY, s ? HIGH : LOW);
        
        if( x != 0 || y != 0 || z != 0 || d != 0 )
        {
          commandCount += 1;
          Serial1.write( 'O' );
          Move( x,y,z,d );
        }
        else if( s >= 0 ) Serial1.write( 'O' );
        else Serial.write( 'E' ); 
        
        x=0;
        y=0;
        z=0;
        d=0;
        i=0;
        s = -1;
        pt = NULL;
        SOF = 0;
        nextIndex++;
      }
      else
      {
        Serial.write( 'E' );
      }
    }
    else if( c == 'R' )
    {
       reset = 2;
       Serial.write( 'O' ); 
    }
    else if( c == 'S' )
    {
      Serial1.print( "X" );
      Serial1.print( X.GetPos( ));
      Serial1.print( "Y" );
      Serial1.print( Y.GetPos( ));
      Serial1.print( "Z" );
      Serial1.print( Z.GetPos( ));
      Serial1.print( "\n" );
    }
    else if( c != 0 && c != '\n' )
    {
       Serial.write( 'E' ); 
    }
  }
  else
  {    
    if( c == 'R' && reset == 3 ) reset = 2;
    else if( c == 'S' && reset == 2 ) reset = 1;
    else if( c == 'T' && reset == 1 )
    {
      Serial1.print( "HLO\n" );
      reset = 0;
      error = 0;
      x=0;
      y=0;
      z=0;
      d=0;
      s=-1;
      pt = NULL;
      X.Reset( );
      Y.Reset( );
      Z.Reset( );
      SOF = 0;
      nextIndex = 0;
    }
    else
    {     
      reset = 3;
    }
  }
}

// ------------------------------------------------------------------------

void setup() {
 
  reset = 3;
  error = 0;
  
  delay( 250 );
  
  LCD_Init( );
  LCD_Clear( );
  delay( 250 );
  
  Serial1.begin(500000);
  
  pinMode(TOOL_ON_REPLAY, OUTPUT);
  digitalWrite(TOOL_ON_REPLAY, LOW);

  X.Reset( );
  Y.Reset( );
  Z.Reset( );
}

void loop() 
{
  while(Serial1.available( ) > 0 )
  {
    Decode(Serial1.read( ));
  }
  LCD_UpdateTask( 100000 );
  LCD_ButtonTask( );
}



