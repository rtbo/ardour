/*
 * Copyright (C) 2021 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef WAF_BUILD
#include "gtk2ardour-config.h"
#endif

#include <list>

#include <gtkmm/label.h>

#include "pbd/properties.h"

#include "gtkmm2ext/gtk_ui.h"
#include "gtkmm2ext/keyboard.h"
#include "gtkmm2ext/window_title.h"

#include "widgets/ardour_spacer.h"

#include "ardour/audio_track.h"
#include "ardour/audioregion.h"
#include "ardour/midi_region.h"
#include "ardour/midi_track.h"
#include "ardour/region_factory.h"
#include "ardour/profile.h"
#include "ardour/smf_source.h"

#include "actions.h"
#include "ardour_ui.h"
#include "editor.h"
#include "gui_thread.h"
#include "public_editor.h"
#include "timers.h"

#include "trigger_page.h"
#include "trigger_strip.h"
#include "ui_config.h"
#include "utils.h"

#include "pbd/i18n.h"

#define PX_SCALE(px) std::max ((float)px, rintf ((float)px* UIConfiguration::instance ().get_ui_scale ()))

using namespace ARDOUR;
using namespace ArdourWidgets;
using namespace Gtkmm2ext;
using namespace Gtk;
using namespace std;

TriggerPage::TriggerPage ()
	: Tabbable (_content, _("Trigger Drom"), X_("trigger"))
	, _cue_area_frame (0.5, 0, 1.0, 0)
	, _cue_box (32, 16 * TriggerBox::default_triggers_per_box)
	, _master_widget (32, 16)
	, _master (_master_widget.root ())
{
	load_bindings ();
	register_actions ();

	/* Match TriggerStrip::_name_button height */
	ArdourButton* spacer = manage (new ArdourButton (ArdourButton::Text));
	spacer->set_name ("mixer strip button");
	spacer->set_sensitive (false);
	spacer->set_text (" ");

	/* left-side, fixed-size cue-box */
	_cue_area_box.set_spacing (2);
	_cue_area_box.pack_start (*spacer, Gtk::PACK_SHRINK);
	_cue_area_box.pack_start (_cue_box, Gtk::PACK_SHRINK);
	_cue_area_box.pack_start (_master_widget, Gtk::PACK_SHRINK);

	/* left-side frame, same layout as TriggerStrip.
	 * use Alignment instead of Frame with SHADOW_IN (2px)
	 * +1px padding for _strip_scroller frame -> 3px top padding
	 */
	_cue_area_frame.set_padding (3, 1, 1, 1);
	_cue_area_frame.add (_cue_area_box);

	_strip_scroller.add (_strip_packer);
	_strip_scroller.set_policy (Gtk::POLICY_ALWAYS, Gtk::POLICY_AUTOMATIC);

	/* Last item of strip packer, "+" background */
	_strip_packer.pack_end (_no_strips, true, true);
	_no_strips.set_flags (Gtk::CAN_FOCUS);
	_no_strips.add_events (Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK);
	_no_strips.set_size_request (PX_SCALE (20), -1);
	_no_strips.signal_expose_event ().connect (sigc::bind (sigc::ptr_fun (&ArdourWidgets::ArdourIcon::expose), &_no_strips, ArdourWidgets::ArdourIcon::ShadedPlusSign));
	_no_strips.signal_button_press_event ().connect (sigc::mem_fun (*this, &TriggerPage::no_strip_button_event));
	_no_strips.signal_button_release_event ().connect (sigc::mem_fun (*this, &TriggerPage::no_strip_button_event));
	_no_strips.signal_drag_motion ().connect (sigc::mem_fun (*this, &TriggerPage::no_strip_drag_motion));
	_no_strips.signal_drag_data_received ().connect (sigc::mem_fun (*this, &TriggerPage::no_strip_drag_data_received));

	std::vector<Gtk::TargetEntry> target_table;
	target_table.push_back (Gtk::TargetEntry ("regions"));
	target_table.push_back (Gtk::TargetEntry ("text/uri-list"));
	target_table.push_back (Gtk::TargetEntry ("text/plain"));
	target_table.push_back (Gtk::TargetEntry ("application/x-rootwin-drop"));
	_no_strips.drag_dest_set (target_table);

	_strip_group_box.pack_start (_cue_area_frame, false, false);
	_strip_group_box.pack_start (_strip_scroller, true, true);

	/* Upper pane ([slot | strips] | file browser) */
	_pane_upper.add (_strip_group_box);
	_pane_upper.add (_trigger_clip_picker);

	/* Bottom -- Properties of selected Slot/Region */
	Gtk::Table* table = manage (new Gtk::Table);
	table->set_homogeneous (false);
	table->set_spacings (8);
	table->set_border_width (8);

	int col = 0;
	table->attach (_slot_prop_box, col, col + 1, 0, 1, Gtk::FILL | Gtk::EXPAND, Gtk::SHRINK);

	col = 1; /* audio and midi boxen share the same table locations; shown and hidden depending on region type */
	table->attach (_audio_trig_box, col, col + 1, 0, 1, Gtk::FILL | Gtk::EXPAND, Gtk::SHRINK);
	++col;
	table->attach (_audio_trim_box, col, col + 1, 0, 1, Gtk::FILL | Gtk::EXPAND, Gtk::SHRINK);
	++col;
	table->attach (_audio_ops_box,  col, col + 1, 0, 1, Gtk::FILL | Gtk::EXPAND, Gtk::SHRINK);
	++col;

	col = 1; /* audio and midi boxen share the same table locations; shown and hidden depending on region type */
	table->attach (_midi_trig_box, col, col + 1, 0, 1, Gtk::FILL | Gtk::EXPAND, Gtk::SHRINK);
	++col;
	table->attach (_midi_trim_box, col, col + 1, 0, 1, Gtk::FILL | Gtk::EXPAND, Gtk::SHRINK);
	++col;
	table->attach (_midi_ops_box,  col, col + 1, 0, 1, Gtk::FILL | Gtk::EXPAND, Gtk::SHRINK);
	++col;

	_parameter_box.pack_start (*table);

	/* Top-level Layout */
	_pane.add (_pane_upper);
	_pane.add (_parameter_box);

	_content.pack_start (_pane, true, true);
	_content.show ();

	/* Show all */
	_pane.show ();
	_pane_upper.show ();
	_strip_group_box.show ();
	_strip_scroller.show ();
	_strip_packer.show ();
	_cue_area_frame.show_all ();
	_trigger_clip_picker.show ();
	_no_strips.show ();

	/* setup keybidings */
	_content.set_data ("ardour-bindings", bindings);

	/* subscribe to signals */
	Config->ParameterChanged.connect (*this, invalidator (*this), boost::bind (&TriggerPage::parameter_changed, this, _1), gui_context ());
	PresentationInfo::Change.connect (*this, invalidator (*this), boost::bind (&TriggerPage::pi_property_changed, this, _1), gui_context ());

	/* init */
	update_title ();

	/* Restore pane state */
	float          fract;
	XMLNode const* settings = ARDOUR_UI::instance ()->trigger_page_settings ();
	if (!settings || !settings->get_property ("triggerpage-vpane-pos", fract) || fract > 1.0) {
		fract = 0.75f;
	}
	_pane.set_divider (0, fract);

	if (!settings || !settings->get_property ("triggerpage-hpane-pos", fract) || fract > 1.0) {
		fract = 0.75f;
	}
	_pane_upper.set_divider (0, fract);
}

TriggerPage::~TriggerPage ()
{
}

Gtk::Window*
TriggerPage::use_own_window (bool and_fill_it)
{
	bool new_window = !own_window ();

	Gtk::Window* win = Tabbable::use_own_window (and_fill_it);

	if (win && new_window) {
		win->set_name ("TriggerWindow");
		ARDOUR_UI::instance ()->setup_toplevel_window (*win, _("Trigger Drom"), this);
		win->signal_event ().connect (sigc::bind (sigc::ptr_fun (&Keyboard::catch_user_event_for_pre_dialog_focus), win));
		win->set_data ("ardour-bindings", bindings);
		update_title ();
#if 0 // TODO
		if (!win->get_focus()) {
			win->set_focus (scroller);
		}
#endif
	}

	contents ().show ();
	return win;
}

XMLNode&
TriggerPage::get_state ()
{
	XMLNode* node = new XMLNode (X_("TriggerPage"));
	node->add_child_nocopy (Tabbable::get_state ());

	node->set_property (X_("triggerpage-vpane-pos"), _pane.get_divider ());
	node->set_property (X_("triggerpage-hpane-pos"), _pane_upper.get_divider ());
	return *node;
}

int
TriggerPage::set_state (const XMLNode& node, int version)
{
	return Tabbable::set_state (node, version);
}

void
TriggerPage::load_bindings ()
{
	bindings = Bindings::get_bindings (X_("TriggerPage"));
}

void
TriggerPage::register_actions ()
{
	Glib::RefPtr<ActionGroup> group = ActionManager::create_action_group (bindings, X_("TriggerPage"));
}

void
TriggerPage::set_session (Session* s)
{
	SessionHandlePtr::set_session (s);

	_cue_box.set_session (s);
	_trigger_clip_picker.set_session (s);
	_master.set_session (s);

	if (!_session) {
		return;
	}

	XMLNode* node = ARDOUR_UI::instance ()->trigger_page_settings ();
	set_state (*node, Stateful::loading_state_version);

	_session->DirtyChanged.connect (_session_connections, invalidator (*this), boost::bind (&TriggerPage::update_title, this), gui_context ());
	_session->StateSaved.connect (_session_connections, invalidator (*this), boost::bind (&TriggerPage::update_title, this), gui_context ());

	_session->RouteAdded.connect (_session_connections, invalidator (*this), boost::bind (&TriggerPage::add_routes, this, _1), gui_context ());
	TriggerStrip::CatchDeletion.connect (*this, invalidator (*this), boost::bind (&TriggerPage::remove_route, this, _1), gui_context ());

	_session->config.ParameterChanged.connect (_session_connections, invalidator (*this), boost::bind (&TriggerPage::parameter_changed, this, _1), gui_context ());

	Editor::instance ().get_selection ().TriggersChanged.connect (sigc::mem_fun (*this, &TriggerPage::selection_changed));

	initial_track_display ();

	_slot_prop_box.set_session (s);

	_audio_trig_box.set_session (s);
	_audio_ops_box.set_session (s);
	_audio_trim_box.set_session (s);

	_midi_trig_box.set_session (s);
	_midi_ops_box.set_session (s);
	_midi_trim_box.set_session (s);

	update_title ();
	start_updating ();
	selection_changed ();
}

void
TriggerPage::session_going_away ()
{
	ENSURE_GUI_THREAD (*this, &TriggerPage::session_going_away);

	stop_updating ();

#if 0
	/* DropReferneces calls RouteUI::self_delete -> CatchDeletion .. */
	for (list<TriggerStrip*>::iterator i = _strips.begin(); i != _strips.end(); ++i) {
		delete (*i);
	}
#endif
	_strips.clear ();

	SessionHandlePtr::session_going_away ();
	update_title ();
}

void
TriggerPage::update_title ()
{
	if (!own_window ()) {
		return;
	}

	if (_session) {
		string n;

		if (_session->snap_name () != _session->name ()) {
			n = _session->snap_name ();
		} else {
			n = _session->name ();
		}

		if (_session->dirty ()) {
			n = "*" + n;
		}

		WindowTitle title (n);
		title += S_("Window|Trigger");
		title += Glib::get_application_name ();
		own_window ()->set_title (title.get_string ());

	} else {
		WindowTitle title (S_("Window|Trigger"));
		title += Glib::get_application_name ();
		own_window ()->set_title (title.get_string ());
	}
}

void
TriggerPage::initial_track_display ()
{
	boost::shared_ptr<RouteList> r = _session->get_tracks ();
	RouteList                    rl (*r);
	_strips.clear ();
	add_routes (rl);
}

void
TriggerPage::selection_changed ()
{
	Selection& selection (Editor::instance ().get_selection ());

	_slot_prop_box.hide ();

	_audio_trig_box.hide ();
	_audio_ops_box.hide ();
	_audio_trim_box.hide ();

	_midi_trig_box.hide ();
	_midi_ops_box.hide ();
	_midi_trim_box.hide ();

	_parameter_box.hide ();

	if (!selection.triggers.empty ()) {
		TriggerSelection ts      = selection.triggers;
		TriggerEntry*    entry   = *ts.begin ();
		TriggerReference ref     = entry->trigger_reference ();
		TriggerPtr       trigger = entry->trigger ();

		_slot_prop_box.set_slot (ref);
		_slot_prop_box.show ();
		if (trigger->region ()) {
			if (trigger->region ()->data_type () == DataType::AUDIO) {
				_audio_trig_box.set_trigger (ref);
				_audio_trim_box.set_region (trigger->region (), ref);

				_audio_trig_box.show ();
				_audio_trim_box.show ();
				_audio_ops_box.show ();
			} else {
				_midi_trig_box.set_trigger (ref);
				_midi_trim_box.set_region (trigger->region (), ref);

				_midi_trig_box.show ();
				_midi_trim_box.show ();
				_midi_ops_box.show ();
			}
		}
		_parameter_box.show ();
	}
}

void
TriggerPage::add_routes (RouteList& rl)
{
	rl.sort (Stripable::Sorter ());
	for (RouteList::iterator r = rl.begin (); r != rl.end (); ++r) {
		/* we're only interested in Tracks */
		if (!boost::dynamic_pointer_cast<Track> (*r)) {
			continue;
		}
#if 0
		/* TODO, only subscribe to PropertyChanged, create (and destory) TriggerStrip as needed.
		 * For now we just hide non trigger strips.
		 */
		if (!(*r)->presentation_info ().trigger_track ()) {
			continue;
		}
#endif

		if (!(*r)->triggerbox ()) {
			/* This Route has no TriggerBox -- and can never have one */
			continue;
		}

		TriggerStrip* ts = new TriggerStrip (_session, *r);
		_strips.push_back (ts);

		(*r)->presentation_info ().PropertyChanged.connect (*this, invalidator (*this), boost::bind (&TriggerPage::stripable_property_changed, this, _1, boost::weak_ptr<Stripable> (*r)), gui_context ());
		(*r)->PropertyChanged.connect (*this, invalidator (*this), boost::bind (&TriggerPage::stripable_property_changed, this, _1, boost::weak_ptr<Stripable> (*r)), gui_context ());
	}
	redisplay_track_list ();
}

void
TriggerPage::remove_route (TriggerStrip* ra)
{
	if (!_session || _session->deletion_in_progress ()) {
		_strips.clear ();
		return;
	}
	list<TriggerStrip*>::iterator i = find (_strips.begin (), _strips.end (), ra);
	if (i != _strips.end ()) {
		_strip_packer.remove (**i);
		_strips.erase (i);
	}
	redisplay_track_list ();
}

void
TriggerPage::redisplay_track_list ()
{
	for (list<TriggerStrip*>::iterator i = _strips.begin (); i != _strips.end (); ++i) {
		TriggerStrip*                strip = *i;
		boost::shared_ptr<Stripable> s     = strip->stripable ();
		boost::shared_ptr<Route>     route = boost::dynamic_pointer_cast<Route> (s);

		bool hidden = s->presentation_info ().hidden ();

		if (!(s)->presentation_info ().trigger_track ()) {
			hidden = true;
		}
		assert (route && route->triggerbox ());
		if (!route || !route->triggerbox ()) {
			hidden = true;
		}

		if (hidden && strip->get_parent ()) {
			/* if packed, remove it */
			_strip_packer.remove (*strip);
		} else if (!hidden && strip->get_parent ()) {
			/* already packed, put it at the end */
			_strip_packer.reorder_child (*strip, -1); /* put at end */
		} else if (!hidden) {
			_strip_packer.pack_start (*strip, false, false);
		}
	}
}

void
TriggerPage::parameter_changed (string const& p)
{
}

void
TriggerPage::pi_property_changed (PBD::PropertyChange const& what_changed)
{
	/* static signal, not yet used */
}

void
TriggerPage::stripable_property_changed (PBD::PropertyChange const& what_changed, boost::weak_ptr<Stripable> ws)
{
	if (what_changed.contains (ARDOUR::Properties::trigger_track)) {
#if 0
		boost::shared_ptr<Stripable> s = ws.lock ();
		/* TODO: find trigger-strip for given stripable, delete *it; */
#else
		/* For now we just hide it */
		redisplay_track_list ();
		return;
#endif
	}
	if (what_changed.contains (ARDOUR::Properties::hidden)) {
		redisplay_track_list ();
	}
}

bool
TriggerPage::no_strip_button_event (GdkEventButton* ev)
{
	if ((ev->type == GDK_2BUTTON_PRESS && ev->button == 1) || (ev->type == GDK_BUTTON_RELEASE && Keyboard::is_context_menu_event (ev))) {
		ARDOUR_UI::instance ()->add_route ();
		return true;
	}
	return false;
}

bool
TriggerPage::no_strip_drag_motion (Glib::RefPtr<Gdk::DragContext> const& context, int, int y, guint time)
{
	context->drag_status (Gdk::ACTION_COPY, time);
	return true;
}

void
TriggerPage::no_strip_drag_data_received (Glib::RefPtr<Gdk::DragContext> const& context, int /*x*/, int y, Gtk::SelectionData const& data, guint /*info*/, guint time)
{
	if (data.get_target () == X_("regions")) {
		boost::shared_ptr<Region> region = PublicEditor::instance ().get_dragged_region_from_sidebar ();
		boost::shared_ptr<TriggerBox> triggerbox;

		if (boost::dynamic_pointer_cast<AudioRegion> (region)) {
			uint32_t output_chan = region->sources().size();
			if ((Config->get_output_auto_connect() & AutoConnectMaster) && session()->master_out()) {
				output_chan =  session()->master_out()->n_inputs().n_audio();
			}
			std::list<boost::shared_ptr<AudioTrack> > audio_tracks;
			audio_tracks = session()->new_audio_track (region->sources().size(), output_chan, 0, 1, region->name(), PresentationInfo::max_order);
			if (!audio_tracks.empty()) {
				triggerbox = audio_tracks.front()->triggerbox ();
			}
		} else if (boost::dynamic_pointer_cast<MidiRegion> (region)) {
			ChanCount one_midi_port (DataType::MIDI, 1);
			list<boost::shared_ptr<MidiTrack> > midi_tracks;
			midi_tracks = session()->new_midi_track (one_midi_port, one_midi_port,
			                                         Config->get_strict_io () || Profile->get_mixbus (),
			                                         boost::shared_ptr<ARDOUR::PluginInfo>(),
			                                         (ARDOUR::Plugin::PresetRecord*) 0,
			                                         (ARDOUR::RouteGroup*) 0, 1, region->name(), PresentationInfo::max_order, Normal, true);
			if (!midi_tracks.empty()) {
				triggerbox = midi_tracks.front()->triggerbox ();
			}
		}

		if (!triggerbox) {
			context->drag_finish (false, false, time);
			return;
		}

		// XXX: check does the region need to be copied?
		boost::shared_ptr<Region> region_copy = RegionFactory::create (region, true);
		triggerbox->set_from_selection (0, region_copy);

		context->drag_finish (true, false, time);
		return;
	}

	std::vector<std::string> paths;
	if (ARDOUR_UI_UTILS::convert_drop_to_paths (paths, data)) {
#ifdef __APPLE__
		/* We are not allowed to call recursive main event loops from within
		 * the main event loop with GTK/Quartz. Since import/embed wants
		 * to push up a progress dialog, defer all this till we go idle.
		 */
		Glib::signal_idle().connect (sigc::bind (sigc::mem_fun (*this, &TriggerPage::idle_drop_paths), paths));
#else
		drop_paths_part_two (paths);
#endif
	}
	context->drag_finish (true, false, time);
}

void
TriggerPage::drop_paths_part_two (std::vector<std::string> paths)
{
	/* compare to Editor::drop_paths_part_two */
	std::vector<string> midi_paths;
	std::vector<string> audio_paths;
	for (std::vector<std::string>::iterator s = paths.begin (); s != paths.end (); ++s) {
		if (SMFSource::safe_midi_file_extension (*s)) {
			midi_paths.push_back (*s);
		} else {
			audio_paths.push_back (*s);
		}
	}
	InstrumentSelector is; // instantiation builds instrument-list and sets default.
	timepos_t pos (0);
	Editing::ImportDisposition disposition = Editing::ImportSerializeFiles; // or Editing::ImportDistinctFiles // TODO use drop modifier? config?
	PublicEditor::instance().do_import (midi_paths, disposition, Editing::ImportAsTrigger, SrcBest, SMFTrackName, SMFTempoIgnore, pos, is.selected_instrument (), false);
	PublicEditor::instance().do_import (audio_paths, disposition, Editing::ImportAsTrigger, SrcBest, SMFTrackName, SMFTempoIgnore, pos);
}

bool
TriggerPage::idle_drop_paths (std::vector<std::string> paths)
{
  drop_paths_part_two (paths);
  return false;
}

gint
TriggerPage::start_updating ()
{
	_fast_screen_update_connection = Timers::super_rapid_connect (sigc::mem_fun (*this, &TriggerPage::fast_update_strips));
	return 0;
}

gint
TriggerPage::stop_updating ()
{
	_fast_screen_update_connection.disconnect ();
	return 0;
}

void
TriggerPage::fast_update_strips ()
{
	if (_content.is_mapped () && _session) {
		for (list<TriggerStrip*>::iterator i = _strips.begin (); i != _strips.end (); ++i) {
			(*i)->fast_update ();
		}
	}
}
