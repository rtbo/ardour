/*
    Copyright (C) 2017 Paul Davis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include "pbd/i18n.h"

#include "ardour/beatbox.h"

#include "beatbox_gui.h"
#include "timers.h"

using namespace ARDOUR;
using namespace Gtkmm2ext;

using std::cerr;
using std::endl;

BBGUI::BBGUI (boost::shared_ptr<BeatBox> bb)
	: ArdourDialog (_("BeatBox"))
	, bbox (bb)
	, step_sequencer_tab_button (_("Steps"))
	, pad_tab_button (_("Pads"))
	, roll_tab_button (_("Roll"))
	, quantize_off (quantize_group, "None")
	, quantize_32nd (quantize_group, "ThirtySecond")
	, quantize_16th (quantize_group, "Sixteenth")
	, quantize_8th (quantize_group, "Eighth")
	, quantize_quarter (quantize_group, "Quarter")
	, quantize_half (quantize_group, "Half")
	, quantize_whole (quantize_group, "Whole")
	, play_button ("Run")
	, clear_button ("Clear")
	, tempo_adjustment (bb->tempo(), 1, 300, 1, 10)
	, tempo_spinner (tempo_adjustment)
{
	setup_pad_canvas ();
	setup_step_sequencer_canvas ();
	setup_roll_canvas ();

	tabs.append_page (step_sequencer_canvas);
	tabs.append_page (pad_canvas);
	tabs.append_page (roll_canvas);
	tabs.set_show_tabs (false);

	quantize_off.signal_toggled().connect (sigc::bind (sigc::mem_fun (*this, &BBGUI::set_quantize), 0));
	quantize_32nd.signal_toggled().connect (sigc::bind (sigc::mem_fun (*this, &BBGUI::set_quantize), 32));
	quantize_16th.signal_toggled().connect (sigc::bind (sigc::mem_fun (*this, &BBGUI::set_quantize), 16));
	quantize_8th.signal_toggled().connect (sigc::bind (sigc::mem_fun (*this, &BBGUI::set_quantize), 8));
	quantize_quarter.signal_toggled().connect (sigc::bind (sigc::mem_fun (*this, &BBGUI::set_quantize), 4));
	quantize_half.signal_toggled().connect (sigc::bind (sigc::mem_fun (*this, &BBGUI::set_quantize), 2));
	quantize_whole.signal_toggled().connect (sigc::bind (sigc::mem_fun (*this, &BBGUI::set_quantize), 1));

	quantize_button_box.pack_start (quantize_off);
	quantize_button_box.pack_start (quantize_32nd);
	quantize_button_box.pack_start (quantize_16th);
	quantize_button_box.pack_start (quantize_8th);
	quantize_button_box.pack_start (quantize_quarter);
	quantize_button_box.pack_start (quantize_half);
	quantize_button_box.pack_start (quantize_whole);

	play_button.signal_toggled().connect (sigc::mem_fun (*this, &BBGUI::toggle_play));
	clear_button.signal_clicked().connect (sigc::mem_fun (*this, &BBGUI::clear));

	misc_button_box.pack_start (play_button);
	misc_button_box.pack_start (clear_button);
	misc_button_box.pack_start (step_sequencer_tab_button);
	misc_button_box.pack_start (pad_tab_button);
	misc_button_box.pack_start (roll_tab_button);

	step_sequencer_tab_button.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &BBGUI::switch_tabs), &step_sequencer_canvas));
	pad_tab_button.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &BBGUI::switch_tabs), &pad_canvas));
	roll_tab_button.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &BBGUI::switch_tabs), &roll_canvas));

	tempo_adjustment.signal_value_changed().connect (sigc::mem_fun (*this, &BBGUI::tempo_changed));

	misc_button_box.pack_start (tempo_spinner);

	get_vbox()->pack_start (misc_button_box, false, false);
	get_vbox()->pack_start (tabs, true, true);
	get_vbox()->pack_start (quantize_button_box, true, true);

	show_all ();
}

BBGUI::~BBGUI ()
{
}

void
BBGUI::update ()
{
	switch (tabs.get_current_page()) {
	case 0:
		update_steps ();
		break;
	case 1:
		update_pads ();
		break;
	case 2:
		update_roll ();
		break;
	default:
		return;
	}
}

void
BBGUI::update_steps ()
{
}

void
BBGUI::update_roll ()
{
}

void
BBGUI::update_pads ()
{
	Timecode::BBT_Time bbt;

	if (!bbox->running()) {
		pads_off ();
		return;
	}

	bbt = bbox->get_last_time ();

	int current_pad_column = (bbt.bars - 1) * bbox->meter_beats ();
	current_pad_column += bbt.beats - 1;

	for (Pads::iterator p = pads.begin(); p != pads.end(); ++p) {
		if ((*p)->col() == current_pad_column) {
			(*p)->rect->set_outline_color (rgba_to_color (1.0, 0.0, 0.0, 1.0));
		} else {
			(*p)->rect->set_outline_color (rgba_to_color (0.0, 0.0, 0.0, 1.0));
		}
	}
}

void
BBGUI::pads_off ()
{
}

void
BBGUI::on_map ()
{
	timer_connection = Timers::rapid_connect (sigc::mem_fun (*this, &BBGUI::update));
	ArdourDialog::on_map ();
}

void
BBGUI::on_unmap ()
{
	timer_connection.disconnect ();
	ArdourDialog::on_unmap ();
}
void
BBGUI::switch_tabs (Gtk::Widget* w)
{
	tabs.set_current_page (tabs.page_num (*w));
}

int BBGUI::Pad::pad_width = 80;
int BBGUI::Pad::pad_height = 80;
int BBGUI::Pad::pad_spacing = 6;

BBGUI::Pad::Pad (ArdourCanvas::Canvas* canvas, int row, int col, int note, std::string const& txt)
	: rect (new ArdourCanvas::Rectangle (canvas, ArdourCanvas::Rect ((col * pad_spacing) + (col * (pad_width - pad_spacing)), (row * pad_spacing) + (row * (pad_height - pad_spacing)),
	                                                                 (col * pad_spacing) + ((col + 1) * (pad_width - pad_spacing)) , (row * pad_spacing) + ((row +1) * (pad_height - pad_spacing)))))
	, text (new ArdourCanvas::Text (canvas))
	, _row (row)
	, _col (col)
	, _note (note)
	, _label (txt)
{
	canvas->root()->add (rect);
	canvas->root()->add (text);

	text->set (_label);

	const ArdourCanvas::Rect r (rect->get());
	text->set_position (ArdourCanvas::Duple (r.x0 + 10, r.y0 + 10));
}

void
BBGUI::Pad::set_color (Gtkmm2ext::Color c)
{
	rect->set_fill_color (c);
	text->set_color (contrasting_text_color (c));
}

void
BBGUI::setup_pad_canvas ()
{
	pad_canvas.set_background_color (Gtkmm2ext::rgba_to_color (0.32, 0.47, 0.89, 1.0));

	size_pads (8, 8);
}

void
BBGUI::size_pads (int cols, int rows)
{
	/* XXX 8 x 8 grid */

	for (Pads::iterator p = pads.begin(); p != pads.end(); ++p) {
		delete *p;
	}

	pads.clear ();
	pad_connections.drop_connections ();

	pad_rows = rows;
	pad_cols = cols;

	for (int row = 0; row < pad_rows; ++row) {

		int note = random() % 128;

		for (int col = 0; col < pad_cols; ++col) {
			Pad* p = new Pad (&pad_canvas, row, col, note, string_compose ("%1", note));
			p->set_color (Gtkmm2ext::rgba_to_color ((random() % 255) / 255.0,
			                                        (random() % 255) / 255.0,
			                                        (random() % 255) / 255.0,
			                                        (random() % 255) / 255.0));

			p->rect->Event.connect (sigc::bind (sigc::mem_fun (*this, &BBGUI::pad_event), col, row));
			pads.push_back (p);
		}
	}
}

bool
BBGUI::pad_event (GdkEvent* ev, int col, int row)
{
	if (ev->type == GDK_BUTTON_PRESS) {
		Timecode::BBT_Time at;

		at.bars = col / bbox->meter_beats();
		at.beats = col % bbox->meter_beats();
		at.ticks = 0;

		at.bars++;
		at.beats++;

		bbox->inject_note (pads[row * pad_cols + col]->note(), 127, at);
		return true;
	}

	return false;
}

void
BBGUI::setup_step_sequencer_canvas ()
{
}

void
BBGUI::setup_roll_canvas ()
{
}

void
BBGUI::tempo_changed ()
{
	float t = tempo_adjustment.get_value();
	bbox->set_tempo (t);
}

void
BBGUI::set_quantize (int divisor)
{
	bbox->set_quantize (divisor);
}

void
BBGUI::clear ()
{
	bbox->clear ();
}

void
BBGUI::toggle_play ()
{
	if (bbox->running()) {
		bbox->stop ();
	} else {
		bbox->start ();
	}
}
