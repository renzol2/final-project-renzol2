// Copyright (c) 2020 [Your Name]. All rights reserved.

#include "covid_sonif_app.h"

#include <iostream>
#include <chrono>
#include <thread>

/*
 * All source code taken from StkTestApp:
 * https://github.com/richardeakin/Cinder-Stk/blob/master/samples/StkTest/src/StkTestApp.cpp
 *
 * This code is meant as a means to test the Cinder-Stk library.
 * By writing it out myself, I hope to gain some insight into how the library
 * works and how I can use it for my own project.
 */
namespace covidsonifapp {

const int kParamsWindowWidth = 300;
const int kParamsWindowHeight = 700;

// 21 and 108 are the lowest/highest keys on the piano keyboard, respectively
const float kMaxPitchMidi = 100;  // originally 80
const float kMinPitchMidi = 30;   // originally 40

const size_t kNumPitchClasses = 12;

const std::vector<std::string> kDataFileNames = {
    R"(C:\Program Files\Cinder\my-projects\final-project-renzol2\assets\data\new_cases.csv)",
    R"(C:\Program Files\Cinder\my-projects\final-project-renzol2\assets\data\total_cases.csv)"
};

using cinder::app::KeyEvent;

/*
 * Overriden Cinder App Methods
 */

CovidSonificationApp::CovidSonificationApp() = default;

void CovidSonificationApp::setup() {
  auto ctx = cinder::audio::master();

  stk::Stk::setSampleRate(ctx->getSampleRate());
  cistk::initRawwavePath();

  master_gain_ = ctx->makeNode<cinder::audio::GainNode>(0.85f);
  master_gain_ >> ctx->getOutput();

  // Mapping generator gain to mouse y value
  generator_gain_ = ctx->makeNode<cinder::audio::GainNode>(0);

  SetupParams();
  HandleInstrumentsSelected();
  HandleEffectSelected();
  HandleDataSelected();
  ctx->enable();

  PrintAudioGraph();
}

void CovidSonificationApp::update() { }

void CovidSonificationApp::draw() {
  cinder::gl::clear();
  params_->draw();
}

void CovidSonificationApp::mouseDown(cinder::app::MouseEvent event) {
  MakeNote(event.getPos());
}

void CovidSonificationApp::mouseDrag(cinder::app::MouseEvent event) {
  MakeNote(event.getPos());
}

void CovidSonificationApp::mouseUp(cinder::app::MouseEvent event) {
  StopNote();
}

/*
 * Stk-Cinder specific methods
 */

void CovidSonificationApp::SetupParams() {
  // Define parameters
  params_ = cinder::params::InterfaceGl::create(
      getWindow(), "STK Params",
      cinder::app::toPixels(
          cinder::ivec2(kParamsWindowWidth, kParamsWindowHeight)));


  // Setup all parameters for each instance variable
  SetupMasterGain();
  SetupInstruments();
  SetupGenerators();
  SetupEffects();
  SetupData();  // region handled only if data is selected
}

void CovidSonificationApp::MakeNote(const cinder::vec2& pos) {
  if (instrument_ && HandleInstrumentSpecificNote(pos))
    return;

  // Get the quantized freq; check if it's significant enough to change
  float freq = QuantizePitch(pos);
  if (std::fabs(last_freq_ - freq) < 0.01f)
    return;

  // Calculate gain
  // float gain = 1.0f - pos.y / (float)getWindowHeight();  // original
  float gain = 0.0f + pos.x / (float)getWindowWidth();

  // Set frequency and gain to instrument/generator accordingly
  if (instrument_) {
    instrument_->noteOn(freq, gain);
  } else if (generator_) {
    generator_gain_->getParam()->applyRamp(gain, 0.05f);

    auto blit_node = std::dynamic_pointer_cast<cistk::BlitNode>(generator_);
    if (blit_node) {
      blit_node->setFrequency(freq);
      return;
    }

    auto granulator_node =
        std::dynamic_pointer_cast<cistk::GranulateNode>(generator_);
    if (granulator_node) return;
  }
}

void CovidSonificationApp::MakeNoteFromAmount(const int amount,
                                              const int max_amount) {
  if (amount == coviddata::kNullAmount) return;

  // Get the quantized freq; check if it's significant enough to change
  float freq = QuantizePitchFromAmount(amount, max_amount);
  if (std::fabs(last_freq_ - freq) < 0.01f) return;

  // Calculate gain (set to 100% for now)
  float gain = 0.85f;

  // TODO: identical to other MakeNote, break into own function
  // Set frequency and gain to instrument/generator accordingly
  if (instrument_) {
    instrument_->noteOn(freq, gain);
  } else if (generator_) {
    generator_gain_->getParam()->applyRamp(gain, 0.05f);

    auto blit_node = std::dynamic_pointer_cast<cistk::BlitNode>(generator_);
    if (blit_node) {
      blit_node->setFrequency(freq);
      return;
    }

    auto granulator_node =
        std::dynamic_pointer_cast<cistk::GranulateNode>(generator_);
    if (granulator_node) return;
  }
}

/**
 * Returns a quantized pitch (in hertz) within the C minor scale
 * @param pos position of mouse
 * @return quantized pitch in C minor scale
 */
float CovidSonificationApp::QuantizePitch(const cinder::vec2& pos) {
  // Define the minor scale
  const size_t scale_length = 7;
  float scale[scale_length] = {0, 2, 3, 5, 7, 8, 10 };

  // Get the MIDI pitch
  // More precisely: creates a mapping from height of mouse on screen to
  //  MIDI pitch, and finds the converted value of the pos.y of mouse
  // This is VERY helpful.
  int pitch_midi = std::lroundf(cinder::lmap(
      pos.y, (float)getWindowHeight(), 0.0f, kMinPitchMidi, kMaxPitchMidi));

  bool quantized = false;

  // Decrease the scale degree of the note until it matches with a diatonic
  // note in the minor key
  while (!quantized) {
    int note = pitch_midi % kNumPitchClasses;
    for (size_t i = 0; i < scale_length; i++) {
      if (note == scale[i]) {
        quantized = true;
        break;
      }
    }
    if (!quantized) {
      pitch_midi--;
    }
  }

  return cinder::audio::midiToFreq((float)pitch_midi);
}

float CovidSonificationApp::QuantizePitchFromAmount(const int amount,
                                                    const int max_amount) {
  // TODO: let the person choose scale
  // Define some scale: minor for now
  const size_t scale_length = 7;
  float scale[scale_length] = { 0, 2, 3, 5, 7, 8, 10 };

  // Creates a mapping from [0, highest amount in data]
  //                     to [min MIDI pitch, max MIDI pitch]
  // Then finds the mapping of the specific data point to a specific MIDI pitch
  int pitch_midi = std::lroundf(cinder::lmap(
      (float)amount, 0.0f, (float)max_amount, kMinPitchMidi, kMaxPitchMidi));

  bool quantized = false;

  // Decrease the scale degree of the note until it matches with a diatonic
  // note in the minor key
  while (!quantized) {
    int note = pitch_midi % kNumPitchClasses;
    for (size_t i = 0; i < scale_length; i++) {
      if (note == scale[i]) {
        quantized = true;
        break;
      }
    }
    if (!quantized)
      pitch_midi--;
  }

  return cinder::audio::midiToFreq((float)pitch_midi);
}

void CovidSonificationApp::HandleInstrumentsSelected() {
  // Disconnect all currently used instruments
  if (instrument_) instrument_->disconnectAll();
  if (generator_)  generator_->disconnectAll();

  generator_enum_selection_ = 0;  // set generator to "none"
  generator_ = nullptr;
  generator_gain_->setValue(0);

  // Get the name of selected instrument and notify user
  std::string name = instrument_names_.at(instrument_enum_selection_);
  CI_LOG_I("Selecting instrument '" << name << "'" );

  // Set instrument accordingly
  auto ctx = cinder::audio::master();
  if (name == "BandedWG") {
    auto instr = ctx->makeNode<cistk::BandedWGNode>();
    instr->setPreset(3);  // preset: 'Tibetan Bowl'
    instrument_ = instr;
  } else if (name == "BlowBotl") {
    instrument_ = ctx->makeNode<cistk::BlowBotlNode>();
  } else if (name == "BlowHole") {
    instrument_ = ctx->makeNode<cistk::BlowHoleNode>();
  } else if (name == "Bowed") {
    instrument_ = ctx->makeNode<cistk::BowedNode>();
  } else if( name == "Brass" ) {
    instrument_ = ctx->makeNode<cistk::BrassNode>();
  } else if( name == "Clarinet" ) {
    instrument_ = ctx->makeNode<cistk::ClarinetNode>();
  } else if( name == "Drummer" ) {
    instrument_ = ctx->makeNode<cistk::DrummerNode>();
  } else if( name == "Flute" ) {
    instrument_ = ctx->makeNode<cistk::FluteNode>();
  } else if( name == "Mandolin" ) {
    instrument_ = ctx->makeNode<cistk::MandolinNode>();
  } else if (name == "Mesh2D") {
    auto instr = ctx->makeNode<cistk::Mesh2DNode>();
    instr->setDecay(0.9f);
    instrument_ = instr;
  } else if( name == "ModalBar" ) {
    auto instr = ctx->makeNode<cistk::ModalBarNode>();
    instr->setPreset( 1 ); // preset: 'Vibraphone'
    instrument_ = instr;
  } else if( name == "Moog" ) {
    instrument_ = ctx->makeNode<cistk::MoogNode>();
  } else if( name == "Plucked" ) {
    instrument_ = ctx->makeNode<cistk::PluckedNode>();
  } else if( name == "Resonate" ) {
    instrument_ = ctx->makeNode<cistk::ResonateNode>();
  } else if( name == "Saxofony" ) {
    instrument_ = ctx->makeNode<cistk::SaxofonyNode>();
  } else if( name == "Shakers" ) {
    instrument_ = ctx->makeNode<cistk::ShakersNode>();
  } else if( name == "Simple" ) {
    instrument_ = ctx->makeNode<cistk::SimpleNode>();
  } else if( name == "Sitar" ) {
    instrument_ = ctx->makeNode<cistk::SitarNode>();
  } else if( name == "StifKarp" ) {
    instrument_ = ctx->makeNode<cistk::StifKarpNode>();
  } else if( name == "VoicForm" ) {
    auto instr = ctx->makeNode<cistk::VoicFormNode>();
    if( ! instr->setPhoneme( "ooo" ) ) {
      CI_LOG_W( "phoneme not found" );
    }
    instr->quiet();
    instrument_ = instr;
  } else if( name == "BeeThree" ) {
    instrument_ = ctx->makeNode<cistk::BeeThreeNode>();
  } else if( name == "FMVoices" ) {
    instrument_ = ctx->makeNode<cistk::FMVoicesNode>();
  } else if( name == "HevyMetl" ) {
    instrument_ = ctx->makeNode<cistk::HevyMetlNode>();
  } else if( name == "PercFlut" ) {
    instrument_ = ctx->makeNode<cistk::PercFlutNode>();
  } else if( name == "Rhodey" ) {
    instrument_ = ctx->makeNode<cistk::RhodeyNode>();
  } else if( name == "TubeBell" ) {
    instrument_ = ctx->makeNode<cistk::TubeBellNode>();
  } else if( name == "Wurley" ) {
    instrument_ = ctx->makeNode<cistk::WurleyNode>();
  } else if( name == "Whistle" ) {
    instrument_ = ctx->makeNode<cistk::WhistleNode>();
  } else {
    CI_LOG_E("Unknown instrument name");
    // CI_ASSERT_NOT_REACHABLE();
  }

  // Set effect, if applicable
  if (effect_) instrument_ >> effect_ >> master_gain_;
  else         instrument_ >> master_gain_;

  master_gain_ >> ctx->getOutput();
}

void CovidSonificationApp::HandleGeneratorSelected() {
  // Disconnect all instruments and generators before reassigning
  if (instrument_) instrument_->disconnectAll();
  if (generator_) generator_->disconnectAll();

  instrument_ = nullptr;
  instrument_enum_selection_ = 0;  // set to "none"

  // Fetch the name of the generator
  std::string name = generator_names_.at(generator_enum_selection_);
  CI_LOG_I("Selecting generator '" << name << "'");

  // Assign the generator based on its name
  auto ctx = cinder::audio::master();

  if (name == "Blit") {
    generator_ = ctx->makeNode<cistk::BlitNode>();
  } else if (name == "Granulate") {
    auto gen = ctx->makeNode<cistk::GranulateNode>(
        1, stk::Stk::rawwavePath() + "ahh.raw", true);  // I don't understand this line...
    generator_ = gen;
  } else {
    CI_LOG_E("Unknown generator name");
    CI_ASSERT_NOT_REACHABLE();
  }

  // Run the generator signal through the effect if applicable
  if (effect_) generator_ >> generator_gain_ >> effect_ >> master_gain_;
  else         generator_ >> generator_gain_ >> master_gain_;

  master_gain_ >> ctx->getOutput();
}

void CovidSonificationApp::HandleEffectSelected() {
  // Effects can only be applied if a sound is generated/played.
  CI_ASSERT( instrument_ || generator_ );

  master_gain_->disconnectAll();
  if( effect_ )
    effect_->disconnectAll();

  // Find the name of the specific effect
  std::string name = effect_names_.at(effect_enum_selection);
  CI_LOG_I("Selecting effect '" << name << "'");

  auto ctx = cinder::audio::master();

  // Assign the effect accordingly
  if( name == "none" ) {
    // reset and bypass effect
    effect_.reset();
    instrument_ >> master_gain_ >> ctx->getOutput();
    if( instrument_ ) instrument_ >> master_gain_ >> ctx->getOutput();
    else              generator_  >> master_gain_ >> ctx->getOutput();
    return;
  } else if( name == "Echo" ) {
    effect_ = ctx->makeNode<cistk::EchoNode>();
  } else if( name == "Chorus" ) {
    effect_ = ctx->makeNode<cistk::ChorusNode>();
  } else if( name == "PitShift" ) {
    auto effect = ctx->makeNode<cistk::PitShiftNode>();
    effect->setShift( 0.5f );
    effect_ = effect;
  } else if( name == "LentPitShift" ) {
    auto effect = ctx->makeNode<cistk::LentPitShiftNode>();
    effect->setShift( 0.5f );
    effect_ = effect;
  } else if( name == "PRCRev" ) {
    effect_ = ctx->makeNode<cistk::PRCRevNode>();
  } else if( name == "JCRev" ) {
    effect_ = ctx->makeNode<cistk::JCRevNode>();
  } else if( name == "NRev" ) {
    effect_ = ctx->makeNode<cistk::NRevNode>();
  } else if( name == "FreeVerb" ) {
    effect_ = ctx->makeNode<cistk::FreeVerbNode>();
  } else {
    CI_LOG_E( "Unknown effect name" );
    CI_ASSERT_NOT_REACHABLE();
  }

  // Play either instrument or generator through effect
  if( instrument_ ) instrument_ >> effect_;
  else              generator_ >> effect_;

  effect_ >> master_gain_ >> ctx->getOutput();
}

void CovidSonificationApp::HandleDataSelected() {
  current_data_.Reset();

  // Find the name of the specific dataset
  std::string name = dataset_names_.at(dataset_selection_);
  CI_LOG_I("Selecting data: '" << name << "'");

  if (name == "none") {
    params_->removeParam("Region");
    params_->removeParam("Sonify!");
  } else {
    // Removing the "Sonify!" button ensures that it appears last in list
    params_->removeParam("Sonify!");

    for (size_t i = 1; i < dataset_names_.size(); i++) {
      if (name == dataset_names_.at(i)) {
        // Indices are offset by 1 because dataset names must
        // include "none" at the beginning.
        const std::string& filename = kDataFileNames.at(i - 1);

        current_data_.ImportData(filename);
        region_names_ = current_data_.GetRegions();
        SetupRegions();

        max_amount_ = GetHighestAmountInData(current_data_, false);

        break;
      }
    }
  }
}

void CovidSonificationApp::HandleRegionSelected() {
  current_region_ = current_data_.GetRegionDataByName(
      region_names_.at(region_selection)
      );
}

/*
 * Private methods
 */

/**
 * Cinder-Stk Documentation: Returns true if the note was handled completely and
 * makeNote() shouldn't do anything else
 *
 * Renzo: I believe this handles an edge case where two specific instruments
 * (being Mesh2D and ModalBar) have to be dealt with individually... not sure tho
 * @param pos position of... mouse?
 */
bool CovidSonificationApp::HandleInstrumentSpecificNote(
    const cinder::vec2& pos) {
  // Not sure what this does yet...
  cinder::vec2 pos_normalized =
      glm::clamp(pos / cinder::vec2(getWindowSize()),
          cinder::vec2(0),
          cinder::vec2(1));

  auto mesh_2d = std::dynamic_pointer_cast<cistk::Mesh2DNode>(instrument_);
  if (mesh_2d) {
    mesh_2d->setInputPosition(pos_normalized.x, pos_normalized.y);
    instrument_->noteOn(0, 1.0f);
    return true;
  }

  auto modal_bar = std::dynamic_pointer_cast<cistk::ModalBarNode>(instrument_);
  if (modal_bar) {
    modal_bar->setStickHardness(pos_normalized.y);
    return false;
  }

  return false;
}

void CovidSonificationApp::PrintAudioGraph() {
  CI_LOG_I("\n" << cinder::audio::master()->printGraphToString());
  std::cout << cinder::audio::master()->printGraphToString() << std::endl;
}

void CovidSonificationApp::SetupMasterGain() {
  // Add master gain as a parameter
  params_
      ->addParam<float>(
          "Master gain", [this](float value) { master_gain_->setValue(value); },
          [this] { return master_gain_->getValue(); })
      .min(0.0f)
      .max(1.0f)
      .step(0.05f);
}

void CovidSonificationApp::SetupInstruments() {
  // Define instrument names
  instrument_names_ = {
      "none",     "BandedWG", "BlowBotl", "BlowHole", "Bowed",    "Brass",
      "Clarinet", "Drummer",  "Flute",    "Mandolin", "Mesh2D",   "ModalBar",
      "Moog",     "Plucked",  "Resonate", "Saxofony", "Shakers",  "Simple",
      "Sitar",    "StifKarp", "VoicForm", "Whistle",  "BeeThree", "FMVoices",
      "HevyMetl", "PercFlut", "Rhodey",   "TubeBell", "Wurley"};

  // Set instruments as a parameter
  params_
      ->addParam("Instrument", instrument_names_,
                 (int*)&instrument_enum_selection_)  // double check this line
      .keyDecr("[")
      .keyIncr("]")
      .updateFn([this] {
        HandleInstrumentsSelected();
        PrintAudioGraph();
      });
}

void CovidSonificationApp::SetupGenerators() {
  // Define generator names
  generator_names_ = {
      "none", "Blit", "Granulate"
  };

  // Set generator as a parameter
  params_
      ->addParam("Generator", generator_names_,
                 (int*)&generator_enum_selection_)
      .keyDecr("u")
      .keyIncr("i")
      .updateFn([this] {
        HandleGeneratorSelected();
        PrintAudioGraph();
      });
}

void CovidSonificationApp::SetupEffects() {
  // Define effects names
  effect_names_ = {
      "none", "Echo", "Chorus", "PitShift", "LentPitShift",
      "PRCRev", "JCRev", "NRev", "FreeVerb"
  };

  // Add effects as parameter
  params_->addParam("Effect", effect_names_, (int*)&effect_enum_selection)
      .keyDecr("o")
      .keyIncr("p")
      .updateFn([this] {
        HandleEffectSelected();
        PrintAudioGraph();
      });
}

void CovidSonificationApp::SetupData() {
  dataset_names_ = {"none", "New cases", "Total cases"};

  params_->addParam("Data", dataset_names_, (int*)&dataset_selection_)
      .keyDecr("t")
      .keyIncr("y")
      .updateFn([this] {
        HandleDataSelected();
        PrintAudioGraph();
      });
}


void CovidSonificationApp::SetupRegions() {
  // Data must be populated for regions to be setup
  if (current_data_.Empty()) return;

  params_->addParam("Region", region_names_, (int*)&region_selection)
      .keyDecr("g")
      .keyIncr("h")
      .updateFn([this] {
        HandleRegionSelected();
        PrintAudioGraph();
      });

  params_->addButton("Sonify!", [this] { SonifyData(); });
}

void CovidSonificationApp::SonifyData() {
  if (dataset_selection_ == 0) return;

  // The max data point only includes the world data if the user wishes to hear
  // the world data sonified.
  // Reason: because world data is the SUM of all other regions' data,
  //  any differences in other regions' data are barely heard.
  max_amount_ = GetHighestAmountInData(
      current_data_, current_region_.GetRegionName() == "World");
  // Equivalent to "zooming in" the scale of a graph

  for (const std::string& date : current_region_.GetDates()) {
    MakeNoteFromAmount(current_region_.GetAmountAtDate(date), max_amount_);

    // Pause for some time
    // TODO: let user choose BPM
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    // TODO: later: visualize
    // visualization code will go here lol (or that may be handled by MakeNote)
  }

  StopNote();
}

int CovidSonificationApp::GetHighestRegionalAmount(
    const coviddata::RegionData& rd) {
  // Find highest note of regional dataset
  int max_amount = 0;

  for (const std::string& date : rd.GetDates()) {
    int amount = rd.GetAmountAtDate(date);
    if (amount > max_amount) max_amount = amount;
  }

  return max_amount;
}

int CovidSonificationApp::GetHighestAmountInData(const coviddata::DataSet& ds,
                                                 bool include_world) {
  int max_amount = 0;

  for (const std::string& region_name : ds.GetRegions()) {
    // Skips world if specified by user
    if (!include_world && region_name == "World") continue;

    coviddata::RegionData rd = ds.GetRegionDataByName(region_name);
    int max_regional_amount = GetHighestRegionalAmount(rd);

    if (max_regional_amount > max_amount) max_amount = max_regional_amount;
  }

  return max_amount;
}

void CovidSonificationApp::StopNote() {
  if (instrument_) {
    instrument_->noteOff(0.5);
  } else if (generator_) {
    generator_gain_->getParam()->applyRamp(0, 0.2f);
  }
}

}  // namespace covidsonifapp
