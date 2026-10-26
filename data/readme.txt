USER MANUAL

0/ Config Mode
Press 3 times reset button not too slow, not too fast, to get into config mode, connect to the Esp32-Setup wifi and open this url http://192.168.4.1(If you have a bonjour compatible browser, you can open any address containing "." and starting with HTTP (not HTTPS), eg "http://re.file" or "http://katapliktika.manitaria/", it's fun!). Fill your ssid/password, ip of superscale, mdns name and save it (or edit these info on the config.json file directly). Restart the board, it will connect to the new network, see 1/. 
	
1/ Startup : connect & sync: turn on
-> red: no wifi or no superscale => turn router on & turn superscale on, 
-> green: found & synced with superscale, good to go

(blink=wait for tag, fast blink=wait for button, ultrafast blink=communicating with superscale)
2/ Scan an ID RFID tag 
-> yellow/orange: superscale has been set with the ID value of the tag
-> red, then green: could not set ID, maybe superscale not available or network reception sucks

3/ Scan a Command RFID tag 
-> yellow/orange: 3 types of tages: regular, transient "#", and set&save "@". 
 - With regular, superscale Command value has been set with the value on the tag. 
 - With transient #, superscale Command is changed to the value of tag, then saved, then the initial command is restored. 
 - With the set&save "@", superscale Command is changed to the value of tag, then saved, but initial command is not restored.
-> red, then green: could not set Command, maybe superscale not available or network reception sucks

4/ Scan an empty RFID tag 
-> white: superscale has generated a new ID (if not disabled) to associate with this RFID tag. On button press, the association is saved and "blockdate=today()" is saved on superscale for that block (unless disabled through UI)
-> red, then green: could not generate a new ID, probably because current ID is not set on superscale to generate from

5a/ Scan "Attach Command" RFID tag
-> purple: current Command on superscale will be attached to the next scanned new tag. Scan again "Attach Command" to cancel. Scan a new tag to attach current Command. Press button to confirm and save. The type of Command tag saved depends on two UI checkboxes, "disable save on rfid/id association" & "transient":
- disable save => classic tag 
- transient + no disable save => transient "#"
- not transient + no disable save => set&save "@"
The command and checkbox settings are fetched when you press the button to confirm the association, so changes made after scanning the super-tag are applied.
-> red, then green: could not set Command. If Command is not empty, maybe superscale is not available or network reception sucks.

6/ Scan "Erase" RFID tag
-> cyan : next scanned tag will be erased on button confirmation. Scan again "Erase" rfid tag to cancel.

7/ Scan "Undo" RFID tag
-> rainbow: on button press, last superscale operation will be cancelled

8/ Scan "RFID-UNDO" RFID tag
-> magenta/rainbow: on button press, last rfid operation (attach/erase) will be cancelled. Scan again "RFID-UNDO" rfid tag to cancel.
-> red: no operation to undo

N.B: If an ID exist on the ID-RFID association, but not saved as block on superscale, it might happen that the new generated ID is this ID and attempting to save will be rejected because the association exist already. In that case, erase the tag or save the block on superscale.




----------------------------

combining distinct hues with controlled lightness/saturation differences improves distinguishability and visual comfort.
https://colorbrewer2.org/#type=qualitative&scheme=Set3&n=9
#a6cee3
#1f78b4
#b2df8a
#33a02c
#fb9a99
#e31a1c
#fdbf6f
#ff7f00
#cab2d6
#6a3d9a
#ffff99
#b15928

#8dd3c7
#ffffb3
#bebada
#fb8072
#80b1d3
#fdb462
#b3de69
#fccde5
#d9d9d9

Okabe-Ito
#E69F00, #56B4E9, #009E73, #F0E442, #0072B2, #D55E00, #CC79A7, #000000

A0 36Hz
| Note | Frequency | Period    |
| ---- | --------- | --------- |
| A−1  | 13.75 Hz  | 72.73 ms  |
| A−2  | 6.875 Hz  | 145.45 ms |
| A−3  | 3.4375 Hz | 290.91 ms |
A−4	1.71875 Hz	2.82 ms
A−5	0.859375 Hz	1.164 s
A−6	0.4296875 Hz	2.327 s
A−7	0.21484375 Hz	4.655 s
