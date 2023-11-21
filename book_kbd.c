/*                                        
Keyboard emulator for Book8088
WIP
Converts HID keyboard codes to PC XT scancodes
(C) 2023 Serhii Liubshin
Skeleton taken from TinyUSB example by Ha Thach (tinyusb.org)
GPLv3
*/

#define  DEBUG

#ifdef DEBUG
# define dprint(x) printf x
#else
# define dprint(x) do {} while (0)
#endif

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

//microseconds
//original value was 6000
//This also defines length of main loop 
const uint64_t IRQ_TIME = 8000;
const uint8_t FIRST_DELAY_CYCLES = 100;
const uint8_t NEXT_DELAY_CYCLES = 10;

uint8_t rep_counter = 0;

uint8_t last_key = 0;
uint8_t repeat_key = 0;

uint8_t local_key = 0;

static void send_key(uint8_t code);

uint8_t non_rep[] = {0x3A, 0x54, 0x46, 0x45, 0x1D, 0x38, 0x2A, 0x36};

uint8_t fifo[17];
uint8_t fifo_ptr = 0;

//---------------------------
uint8_t main_cycle(void) { 
uint8_t code;
  //Do we have something in FIFO?
  if (fifo_ptr>0) {
      code = fifo[fifo_ptr--];
  } else code = last_key;
 
  if (!code) return 0;

  if (rep_counter) rep_counter--;

  if (code&0x80) {
      if ((code&0x7F)==last_key) {
          dprint(("Last key %X depressed\r\n",code&0x7F));
          last_key = 0;
      }
      dprint(("REL_%X ",code&0x7F));
      return code;
  } else {
      if (code != last_key) {
          repeat_key = code;
          last_key = code;
          for (uint8_t i=0; i<sizeof(non_rep); i++) if (code==non_rep[i]) repeat_key = 0;
          if (repeat_key) { 
              dprint(("FIR_%X ",code));
              rep_counter = FIRST_DELAY_CYCLES;
          } else {
              dprint(("NOR_%X ",code));
          }
          return code;
      } else {
          if (!repeat_key) return 0;
          if (!rep_counter) {
              dprint(("NER_%X ",code));
              rep_counter = NEXT_DELAY_CYCLES;
              return code;
          } else return 0;
      }
  }
}

void clear_pins(void) {
  for (int i=0;i<8;i++) gpio_put(kbd_out_pins[i],0);
  fifo_ptr = 0;
  last_key = 0;
} 

void raise_interrupt(uint8_t code) {
  //Set keyboard pins
  //2Do - only if key changes
  for (int i=0;i<8;i++) {
      (code&1) ? gpio_put(kbd_out_pins[i],1) : gpio_put(kbd_out_pins[i],0);
      code = code>>1;
  }  
  gpio_put(int_pin,1);
  sleep_us(IRQ_TIME>>1);
}

void lower_interrupt(void) {
    gpio_put(int_pin,0);
    sleep_us(IRQ_TIME>>1);
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

tuh_init(BOARD_TUH_RHPORT);

//Main loop
  while (true)
  {
      tuh_task();
      //Built-in keyboard
      //External keyboard is processed in handlers
      get_input();
      uint8_t code = main_cycle();
      if (code) raise_interrupt(code);
      lower_interrupt();
      //Free CPU a bit
      sleep_us(20);
  }

  return 0;

}

void get_input(void) {
/*
Native timings:
6'100us - interrupt time
620'700us - fist key repeat
61'500us - next key repeat
*/

//We can actually do
if (kbd_conn) return;

uint8_t code;
  //recreate scancode from pins
  for (int i=0;i<8;i++) {
  code=code>>1;
  if (gpio_get(kbd_in_pins[i])) code=code|0x80;
  }

  if (local_key == code) return;

  local_key = code;

  //0 ptr means buffer empty, so we use 1-17 for 16 byte buffer
  if (fifo_ptr<17) {
    fifo[++fifo_ptr] = code;
  } else {
      printf("Buffer full!\r\n");  
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
  clear_pins();
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
  clear_pins();
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

  //0 ptr means buffer empty, so we use 1-17 for 16 byte buffer
  if (fifo_ptr<17) {
    fifo[++fifo_ptr] = code;
  } else {
      printf("Buffer full!\r\n");  
  }

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