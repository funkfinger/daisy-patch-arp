# Daisy Info

This file is here mostly for AI so that I don't have to explain the Daisy platform.

- This project is using a Daisy patch.init() module.
  - The Daisy patch.init() module uses a Daisy Patch SM.
  - The Patch SM pinout is in `./Patch-SM_pinout.csv`

Here is some text copied from the Daisy patch.init() module schematic.

The generic comm. protocols are connected directly to the processor. This means they can be used for standard functions including switches, LEDs, etc.

Power connections can go directly to the pins of the same name on the Daisy Patch SM. Reverse protection is implemented on the submodule. However, if adding external components that would be sensitive to damage, it is recommended to add your own reverse protection.

- The CV in pins are -5V to 5V inputs
- The CV out pins are 0-5V output
- The Gate in pins are connected to BJT circuits
- The Gate out pins are identical to the CV out circuit

Pots are wired 0-5V.They could be wired between -5V and 5V if there is a negative 5V source present. This would increase the resolution of the input from 15-bits (32768 values) to 16-bits (65536 values).

Daisy has configurable pull up/down resistorsfor the GPIO pins. This allows us to connect switches to GND with an internal pull up. When the switch is an unconnected state, the pullup sets it to 3v3, and when pressed it connects to GND.

The CV and GATE outputs have output resistors on the Daisy Patch SM, allowing us to connect LEDs without any series extra components.

The SDMMC_CMD/D# pins on the Daisy Patch SM have 47K Pullup resistors on board. This allows connection directly to an SD Card slot connector with noadditional components. These pins can be reused as standard GPIO, just keep in mind that they do have a pull up installed.
