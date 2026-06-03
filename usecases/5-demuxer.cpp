dmx = qcap2_demuxer_new()
evt = qcap2_event_new()
qcap2_demuxer_set_event(dmx, evt)
qcap2_demuxer_set_type(dmx,xxx)

evt_handlers = qcap2_event_handlers_new()
qcap2_event_handlers_add_handler(evt_handlers, evt, on_dmx_event)
qcap2_event_handlers_start(evt_handlers)

qcap2_demuxer_start(dmx)

on_dmx_event() {
    qcap2_event_wait(evt)

    // stop all started a/v source/encoders

    qcap2_demuxer_update(dmx)
    // update demuxed parameters 
    // like width,height,sample rate,etc.
    // to a/v source/encoder
    prog = qcap2_demuxer_get_program_info(dmx)
    // get a/v source/encoder from prog
    // re-config a/v source/encoder
    // start a/v source/encoder

    qcap2_demuxer_play(dmx)
    // now, dmx demux a/v frame/packet to a/v source/encoders
}
