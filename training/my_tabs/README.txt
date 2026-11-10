DRUM TABS -> THE HAMMER'S NEURAL BRAIN
======================================
Put drum tab text files (.txt) in the folder of the style they belong to,
e.g.  funk/funky_drummer_break.txt , rock/my_verse_beat.txt

Tabs teach the brain WHICH notes to play. How the notes FEEL (pushing and
dragging, exact loudness) is learned only from the real drummers in the
Groove MIDI Dataset and your own MIDI - so tabs never make it sound robotic.

Check a tab was understood:   python training/tab_reader.py <file> --midi check.mid
(or just run the training - the log lists every tab it read and any warnings)

LAYOUT
    title: My groove          <- optional
    style: funk               <- optional (otherwise the folder name is used)
    tempo: 96                 <- optional (only used for check.mid)

       1e+a2e+a3e+a4e+a
    HH|xxxxxoxxxxxxxoxx|
    SN|----O--g-g-gO--g|
    B |o-o-------o--o--|  x4      <- "x4" repeats the block 4 times

  * one line per drum: a label, then bars between | marks
  * lines stacked together = one block; a blank line starts the next block
  * 16 characters per bar = 16th notes, 12 = triplets (shuffle/swing), 8 = 8ths, 32 = 32nds
  * rests: -  .  or a space.  Count lines and any other text are ignored.
  * 4/4 only for now

LABELS                      SYMBOLS
  CC / C / CR  crash          cymbals:  x hit   X accent   o open hi-hat   O accented open
  C2           crash 2                  b ride bell   c china   s splash   # choke (silent)
  HH / H       hi-hat                   g soft
  Hf / HF      hi-hat foot   drums:    o hit   O accent   g ghost note   f flam   d drag
  Rd / R       ride                     b soft roll / buzz   B accented roll
  CH           china                    x or @ on the snare line = cross-stick / rim
  SP           splash
  SN / S / SD  snare        Cajon folder: kick = bass tone, snare = slap, snare x = finger tip
  T1 / HT      high tom     Orchestral:  toms = timpani, ride = suspended cymbal,
  T2 / MT      mid tom                   crash = clash cymbals, china = gong
  FT / F / T3  floor tom
  B / BD / K   kick
  CB cowbell, TA tambourine, SH shaker

Your repository is public: anything in here can be seen by anyone. Grooves and
patterns are fine; keep full transcriptions of famous records somewhere private.
