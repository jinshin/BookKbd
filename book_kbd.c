/*
Keyboard emulator for Book8088
WIP
Converts HID keyboard codes to PC XT scancodes
(C) 2023 Serhii Liubshin
Skeleton taken from TinyUSB example by Ha Thach (tinyusb.org)
GPLv3
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bsp/board.h"
#include "tusb.h"

#include "hardware/gpio.h"

#include "xt.h"

uint8_t  kbd_out_pins[8] = {2,3,4,5,6,7,8,9};
uint8_t  kbd_in_pins[8] = {11,12,13,14,15,26,27,28};
uint8_t  int_pin = 10;
uint8_t  kbd_conn = 0;
uint8_t  prev_code = 0;

uint8_t  int1_state = 0;
uint64_t prevclock = 0;

//microseconds
//original value was 6000
const uint64_t IRQ_TIME = 3000;
const uint64_t FIRST_DELAY = 600000;
const uint64_t NEXT_DELAY = 60000;

uint8_t int_active = 0;
uint64_t int_start = 0;

uint8_t pressed_key = 0;
uint8_t repeat_key = 0;
uint8_t first_repeat = 0;
uint64_t key_time = 0;

static void send_key(uint8_t code);

uint8_t non_rep[] = {0x3A, 0x54, 0x46, 0x45, 0x1D, 0x38, 0x2A, 0x36};

void raise_interrupt(uint8_t code) {
  //if interrupt is still running - we need to cancel it and sleep a bit to aknowledge
  if (int_active) {
      sleep_us(IRQ_TIME-(time_us_64() - int_start));
      gpio_put(int_pin,0);
      sleep_us(IRQ_TIME);
      int_active = 0;
  }

  printf("H-%X",code);

  //Set keyboard pins
  for (int i=0;i<8;i++) {
      (code&1) ? gpio_put(kbd_out_pins[i],1) : gpio_put(kbd_out_pins[i],0);
      code = code>>1;
  }
  
  gpio_put(int_pin,1);
  int_start = time_us_64();
  int_active = 1;
}

void lower_interrupt(void) {
  if (!int_active) return;
  uint64_t i_time = time_us_64() - int_start;
  if (i_time > IRQ_TIME) {
      gpio_put(int_pin,0);
      int_active = 0;
      printf("-L ");
      int_start = time_us_64();
  }
}

void key_repeat(void) {

  if (!pressed_key) return;

  if (pressed_key&0x80) {
  //Process key release
      if ((pressed_key&0x7F)==repeat_key) {
      //Mimic original behaviour
      //Last key repeats are not reset if other key released
          raise_interrupt(pressed_key);
          pressed_key = 0;
          repeat_key = 0;	
      } else {
      //Notify depress of other key, but keep pattern for currently pressed key         
          raise_interrupt(pressed_key);
          //Fixme
          sleep_us(IRQ_TIME);
          pressed_key = repeat_key;
          key_time = time_us_64();
      }
  } else {
          //if (pressed_key!=repeat_key) { repeat_key=pressed_key; raise_interrupt(); }
  //Process key press
      if (pressed_key!=repeat_key) {
          first_repeat = 1;
          key_time = time_us_64();
          raise_interrupt(pressed_key);
          repeat_key = pressed_key;
      } else {
          //Skip non-repeating keys
          for (uint8_t i=0; i<sizeof(non_rep); i++) {
              if (pressed_key==non_rep[i]) return;
	  }
	  //Do repeats
          uint64_t elapsed_time = time_us_64() - key_time;
          //No need to get further if delay less than minimum
          if (elapsed_time < NEXT_DELAY ) return;
          //Next repeat
          if (!first_repeat) { key_time = time_us_64(); raise_interrupt(pressed_key); return; }
          //First repeat
          if (elapsed_time > FIRST_DELAY ) { key_time = time_us_64(); first_repeat=0; raise_interrupt(pressed_key); return; }
      }
  }
}


void hid_app_task(void);
void get_input(void);
static void process_kbd_report(hid_keyboard_report_t const *report);

int main(void)
{
  board_init();

  printf("External keyboard support for Book8088\r\n");
  printf("(C) 2023 Serhii Liubshin\r\n");

//We use pins 2,3,4,5,6,7,8,9 for bits
//Pin 10 - to signal interrupt

  for (int i=0;i<8;i++) {
      gpio_init(kbd_out_pins[i]);
      gpio_set_dir(kbd_out_pins[i],GPIO_OUT);
  }
  gpio_init(int_pin);
  gpio_set_dir(int_pin,GPIO_OUT);

  for (int i=0;i<8;i++) {
      gpio_init(kbd_in_pins[i]);
      gpio_set_dir(kbd_in_pins[i],GPIO_IN);
  }


//Temp code to analyze
//	gpio_init(15);
//	gpio_set_dir(15,GPIO_IN);
//-----

  tuh_init(BOARD_TUH_RHPORT);

prevclock = time_us_64();

//Main loop
  while (true)
  {
      tuh_task();
      //Built-in keyboard
      //External keyboard is processed in handlers
      get_input();
      //Process interrupt cycle
      lower_interrupt();
      //Process key repeat cycle
      key_repeat();
      //Free CPU a bit
      sleep_us(20);
  }

  return 0;

}

/*
void send_interrupt() {
  //Signal interrupt
  gpio_put(int_pin,1);
  //need to determine this time, in microseconds
  //i've seen 6130us but that's way too much
  sleep_us(1000) ;
  gpio_put(int_pin,0);
}
*/

void get_input(void) {
/*
if (!kbd_conn) {
  uint8_t inp = gpio_get(15);
	if (inp!=int1_state) {
	uint64_t cclock = time_us_64();
	   int1_state=inp;
	   printf("Interrupt: %d, Delay: %llu us\r\n",inp,(cclock-prevclock));
	prevclock = cclock;
	}
  }
*/

/*
Native timings:
6'100us - interrupt time
620'700us - fist key repeat
61'500us - next key repeat
*/

/*
Behaviour patterns:
We hold a key - send interrupt, long delay, send interrupt, short delay, send interrupt, short, etc.
Release same key - send interrupt
Release key we press and still hold before current repeating key - send interrupt, continue send for current key
*/

if (kbd_conn) return;

uint8_t code;
  //recreate scancode from pins
  for (int i=0;i<8;i++) {
  code=code>>1;
  if (gpio_get(kbd_in_pins[i])) code=code|0x80;
  }

  if (prev_code!=code) {
      printf("Key: %X\r\n", code);
      prev_code = code;
      send_key(code);
  }

}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

// called after all tuh_hid_mount_cb
void tuh_mount_cb(uint8_t dev_addr)
{
  // application set-up
  // printf("A device with address %d is mounted\r\n", dev_addr);
}

// called before all tuh_hid_unmount_cb
void tuh_umount_cb(uint8_t dev_addr)
{
  // application tear-down
  // printf("A device with address %d is unmounted \r\n", dev_addr);
}


// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it will be skipped
// therefore report_desc = NULL, desc_len = 0

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
//  printf("HID device address = %d, instance = %d is mounted\r\n", dev_addr, instance);
  board_led_write(1);
  gpio_init(16);
  gpio_put(16,1);
  kbd_conn = 1;
  pressed_key = 0;
  repeat_key = 0;
  if(tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD) {
    if ( !tuh_hid_receive_report(dev_addr, instance) )
    {
      printf("Error: cannot request to receive report\r\n");
    }
  }
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  board_led_write(0);
  gpio_put(16,0);
  kbd_conn = 0;
  pressed_key = 0;
  repeat_key = 0;
  //printf("HID device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);
}

//For setting leds
uint8_t kbd_addr;
uint8_t kbd_inst;

// Invoked when received report from device via interrupt endpoint (key down and key up)
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  switch (itf_protocol)
  {
    case HID_ITF_PROTOCOL_KEYBOARD:
      kbd_addr = dev_addr;
      kbd_inst = instance;
      process_kbd_report((hid_keyboard_report_t const*) report );
    break;
  }

  // continue to request to receive report
  if ( !tuh_hid_receive_report(dev_addr, instance) )
  {
    printf("Error: cannot request to receive report\r\n");
  }
}


//--------------------------------------------------------------------+
// Keyboard
//--------------------------------------------------------------------+

static void send_key(uint8_t code)
{

  static uint8_t numlock_state = 0;
  static uint8_t caps_state = 0;
  static uint8_t scroll_state = 0;

  static uint8_t set_leds = 0;
  //Skip zeros
  if (!(code&0x7F)) return;

  //Process NumLock, CapsLock, ScrollLock
  //0x45, 0x3A, 0x46

  set_leds = 0;

  if (code==0x45) {
    set_leds = 1;
    numlock_state = numlock_state ^ 0x1;    
  }

  if (code==0x3A) {
    set_leds = 1;
    caps_state = caps_state ^ 0x1;    
  }

  if (code==0x46) {
    set_leds = 1;
    scroll_state = scroll_state ^ 0x1;    
  }

  if (set_leds) {
     set_leds = (scroll_state<<2)|(caps_state<<1)|numlock_state;
     //printf("LEDS:%X  ",set_leds);
     //guess who used not static variable here? :)
     tuh_hid_set_report(kbd_addr,kbd_inst,0,HID_REPORT_TYPE_OUTPUT,&set_leds,1);
  }

  //printf("S: %X ",code);

  pressed_key = code;

  //send_interrupt();

}

static void process_kbd_report(hid_keyboard_report_t const *report)
{

/*
0x1D ;CTRL 0x01 | 0x10 ;Left and Right are the same on XT
0x38 ;ALT 0x04 | 0x40 ;Left and Right are the same on XT
0x2A ;Left SHIFT 0x02
0x36 ;Right SHIFT 0x20
*/

//Only six simultaneous keys :(

  static uint8_t prev_keys[6] = {0};

  static uint8_t prev_modifiers = 0; // previous modifier

  static uint8_t prev_ctrl_state = 0;
  static uint8_t prev_alt_state = 0;
  static uint8_t prev_shiftl_state = 0;
  static uint8_t prev_shiftr_state = 0;

  uint8_t ctrl_state = 0;
  uint8_t alt_state = 0;
  uint8_t shiftl_state = 0;
  uint8_t shiftr_state = 0;

  uint8_t modifiers = report->modifier;

  ctrl_state = (modifiers & 0x11) ? 1 : 0;
  alt_state = (modifiers & 0x44) ? 1 : 0;
  shiftl_state = (modifiers & 0x2) ? 1 : 0;
  shiftr_state = (modifiers & 0x20) ? 1 : 0;

  if (ctrl_state!=prev_ctrl_state) {
    ctrl_state ? send_key(CTRL) : send_key(CTRL|0x80);
    prev_ctrl_state = ctrl_state;
  }

  if (alt_state!=prev_alt_state) {
    alt_state ? send_key(ALT) : send_key(ALT|0x80);
    prev_alt_state = alt_state;
  }

  if (shiftl_state!=prev_shiftl_state) {
    shiftl_state ? send_key(SHIFTL) : send_key(SHIFTL|0x80);
    prev_shiftl_state = shiftl_state;
  }  

  if (shiftr_state!=prev_shiftr_state) {
    shiftr_state ? send_key(SHIFTR) : send_key(SHIFTR|0x80);
    prev_shiftr_state = shiftr_state;
  }  

  uint8_t i,j,keycode,send_event;

//Process key release first
  for (i=0;i<6;i++) {
    keycode = prev_keys[i];
    //Skip zeros
    if (keycode && (keycode<sizeof(HID2XT))) {
      send_event=1;
      for (j=0;j<6;j++) {
        //if keycode exist, key is still pressed, if not - we need to send a release.
	//I wonder what expected to happen if one presses seven+ keys at once
        if (keycode == report->keycode[j]) { send_event = 0; break; }
      }
      if (send_event) send_key(HID2XT[keycode]|0x80);
    }
  }

//Process key press
  for (i=0;i<6;i++) {
    keycode = report->keycode[i];
    //Skip zeros
    if (keycode && (keycode<sizeof(HID2XT))) {
      send_event=1;
      for (j=0;j<6;j++) {
        //Same logic here
        if (keycode == prev_keys[j]) { send_event = 0; break; }
      }
      if (send_event) send_key(HID2XT[keycode]);
    }
  }

//Save state
  for (i=0;i<6;i++) prev_keys[i]=report->keycode[i];

}