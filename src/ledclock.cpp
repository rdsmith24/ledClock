// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Example of a clock. This is very similar to the text-example,
// except that it shows the time :)
//
// This code is public domain
// (but note, that the led-matrix library this depends on is GPL v2)

#include "led-matrix.h"
#include "graphics.h"

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

using namespace rgb_matrix;

/////////////////////////////////////////////////////////////////////
// Setup an interrupt handeler to detect if we are being told to quit
volatile bool interrupt_received = false;

static void InterruptHandler(int signo)
{
  interrupt_received = true;
}
/////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
// This function is called when we have gotten something bad on the
// command line aruguments
static int usage(const char *progname)
{
  fprintf(stderr, "usage: %s [options]\n", progname);
  fprintf(stderr, "Reads text from stdin and displays it. "
                  "Empty string: clear screen\n");
  fprintf(stderr, "Options:\n");
  rgb_matrix::PrintMatrixFlags(stderr);
  fprintf(stderr,
          "\t-t <time-format>  : Default '%%H:%%M'. See strftime()\n"
          "\t-d <date-format>  : Default '%%a,%%b%%d'. See strftime()\n"
          "\t-C <r,g,b>        : Time color. Default 255,223,0\n"
          "\t-c <r,g,b>        : Date color. Default 255,69,0\n"
          "\t-f <font-file>    : Time/Date font. Default ./7x13.dbf\n"
          "\t-x <x-origin>     : X-Origin of displaying clock text (Default: 4)\n"
          "\t-y <y-origin>     : Y-Origin of displaying clock text (Default: 0)\n"
          "\t-b <brightness>   : Sets brightness percent. Default: 15.\n"
          "\t-S <spacing>      : Spacing pixels between letters (Default: 0)\n"
          "\t-B <r,g,b>        : Background-Color. Default 0,0,0\n"
          "\t-O <r,g,b>        : Outline-Color, e.g. to increase contrast.\n");
  return 1;
}
/////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
// When we are passed a color specification on the CLI, check to make sure its okay
static bool parseColor(Color *c, const char *str)
{
  return sscanf(str, "%hhu,%hhu,%hhu", &c->r, &c->g, &c->b) == 3;
}

static bool FullSaturation(const Color &c)
{
  return (c.r == 0 || c.r == 255) && (c.g == 0 || c.g == 255) && (c.b == 0 || c.b == 255);
}

int main(int argc, char *argv[])
{
  // Setup the matrix panel with some options here.  If the user wants to override, then they will be overwritten when parsing the command line
  RGBMatrix::Options matrix_options;
  matrix_options.cols = 64;                             // Set number of columns to 64
  matrix_options.rows = 32;                             // Set number of rows to 65
  matrix_options.hardware_mapping = "adafruit-hat-pwm"; // Setup to use Adafruit Matrix Bonnet with PWM
  matrix_options.led_rgb_sequence = "RGB";              // Remap colors from RGB to RBG
  matrix_options.show_refresh_rate = false;              // Show the refresh rate
  matrix_options.brightness = 15;                       // Set brightness to 50
  rgb_matrix::RuntimeOptions runtime_opt;
  if (!rgb_matrix::ParseOptionsFromFlags(&argc, &argv,
                                         NULL, &runtime_opt)) // matrix options were set manually so pass NULL pointer to matrix_options
  {
    // If you put someing in wrong, print out the options
    return usage(argv[0]);
  }

  // Set some default options for formats, colors and fonts
  const char *time_format = "%I:%M:%S";
  const char *date_format = "%a,%b%d";
  Color timeColor(255, 223, 0); // Color of the time line
  Color dateColor(255, 69, 0);  // Color of the date line
  Color bg_color(0, 0, 0);
  Color outline_color(0, 0, 0);
  Color weatherLineColor(250, 128, 114);
  bool with_outline = false;
  const char *clockFont_font_file = "./7x13.bdf";

  int x_orig = 4; // Origin of time line
  int y_orig = 0;
  int brightness = 100;
  int letter_spacing = 0;

  char time_buffer[256];
  char date_buffer[256];

  struct timespec next_time;
  next_time.tv_sec = time(NULL);
  next_time.tv_nsec = 0;
  struct tm tm;
  ssize_t result;

  /////////////////////////////////////////////////////////////////////
  // Read out CLI arguments
  int opt;
  while ((opt = getopt(argc, argv, "t:d:B:O:b:S:x:y:f:C:c:")) != -1)
  {
    switch (opt)
    {
    case 't':
      time_format = strdup(optarg);
      break;
    case 'd':
      date_format = strdup(optarg);
      break;
    case 'x':
      x_orig = atoi(optarg);
      break;
    case 'y':
      y_orig = atoi(optarg);
      break;
    case 'b':
      brightness = atoi(optarg);
      break;
    case 'S':
      letter_spacing = atoi(optarg);
      break;
    case 'f':
      clockFont_font_file = strdup(optarg);
      break;
    case 'B':
      if (!parseColor(&bg_color, optarg))
      {
        fprintf(stderr, "Invalid background color spec: %s\n", optarg);
        return usage(argv[0]);
      }
      break;
    case 'O':
      if (!parseColor(&outline_color, optarg))
      {
        fprintf(stderr, "Invalid outline color spec: %s\n", optarg);
        return usage(argv[0]);
      }
      with_outline = true;
      break;
    case 'C':
      if (!parseColor(&timeColor, optarg))
      {
        fprintf(stderr, "Invalid time color spec: %s\n", optarg);
        return usage(argv[0]);
      }
      break;
    case 'c':
      if (!parseColor(&dateColor, optarg))
      {
        fprintf(stderr, "Invalid date color spec %s\n", optarg);
        return usage(argv[0]);
      }
      break;
    default:
      return usage(argv[0]);
    }
  }

  // Try to load the clock font file, bail out of not avaliable
  if (clockFont_font_file == NULL)
  {
    fprintf(stderr, "Need to specify time/date clock font BDF font-file with -f\n");
    return usage(argv[0]);
  }

  /////////////////////////////////////////////////////////////////////
  // Load time/date font. This needs to be a filename with a bdf bitmap font.
  rgb_matrix::Font clockFont;
  if (!clockFont.LoadFont(clockFont_font_file))
  {
    fprintf(stderr, "Couldn't load time/date clock font file '%s'\n", clockFont_font_file);
    return 1;
  }
  rgb_matrix::Font *outline_font = NULL;
  if (with_outline)
  {
    outline_font = clockFont.CreateOutlineFont();
  }

  /////////////////////////////////////////////////////////////////////
  // See if the brighness is within range
  if (brightness < 1 || brightness > 100)
  {
    fprintf(stderr, "Brightness is outside usable range.\n");
    return 1;
  }

  // Create the matrix with options
  RGBMatrix *matrix = rgb_matrix::CreateMatrixFromOptions(matrix_options,
                                                          runtime_opt);
  if (matrix == NULL)
    return 1;

  // Set the brightness from default or CLI
  matrix->SetBrightness(brightness);

  const bool all_extreme_colors = (brightness == 100) && FullSaturation(timeColor) && FullSaturation(bg_color) && FullSaturation(outline_color);
  if (all_extreme_colors)
    matrix->SetPWMBits(1);

  /////////////////////////////////////////////////////////////////////
  // Create something for us to draw on
  FrameCanvas *offscreen = matrix->CreateFrameCanvas();

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  /////////////////////////////////////////////////////////////////////
  // Start looping until shutdown
  while (!interrupt_received)
  {
    localtime_r(&next_time.tv_sec, &tm);
    strftime(time_buffer, sizeof(time_buffer), time_format, &tm);
    strftime(date_buffer, sizeof(date_buffer), date_format, &tm);

    offscreen->Fill(bg_color.r, bg_color.g, bg_color.b);
    if (outline_font)
    {
      rgb_matrix::DrawText(offscreen, *outline_font,
                           x_orig - 1, y_orig + clockFont.baseline(),
                           outline_color, NULL, time_buffer,
                           letter_spacing - 2);
    }

    rgb_matrix::DrawText(offscreen, clockFont, x_orig, y_orig + clockFont.baseline(),
                         timeColor, NULL, time_buffer,
                         letter_spacing);
    rgb_matrix::DrawText(offscreen, clockFont, 0, 14 + clockFont.baseline(),
                         dateColor, NULL, date_buffer,
                         letter_spacing);
    

    // Wait until we're ready to show it.
    clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &next_time, NULL);

    // Atomic swap with double buffer
    offscreen = matrix->SwapOnVSync(offscreen);

    next_time.tv_sec += 1;
  }

  // Finished. Shut down the RGB matrix.
  matrix->Clear();
  delete matrix;

  /////////////////////////////////////////////////////////////////////
  // Create a fresh new line after ^C on screen
  result = write(STDOUT_FILENO, "\n", 1);
  result += 0;

  return 0;
}
