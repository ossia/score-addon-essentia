// Native Essentia API experiment. This executable does not register score processes.
#include <algorithmfactory.h>
#include <version.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
using Factory = essentia::standard::AlgorithmFactory;
using Algorithm = essentia::standard::Algorithm;
using Real = essentia::Real;

QString text(const std::string& s) { return QString::fromStdString(s); }

void require(bool condition, const char* message)
{
  if(!condition)
    throw std::runtime_error(message);
}

template <typename F>
QJsonArray listAlgorithms()
{
  QJsonArray result;
  for(const auto& name : F::keys())
  {
    const auto& info = F::getInfo(name);
    result.append(QJsonObject{{"name", text(name)}, {"category", text(info.category)},
                              {"description", text(info.description)}});
  }
  return result;
}

template <typename F>
QJsonObject describe(const std::string& name, bool raw)
{
  // getInfo() uses map::operator[]: reject unknown names before calling it.
  const auto names = F::keys();
  require(std::find(names.begin(), names.end(), name) != names.end(), "Unknown algorithm");
  const auto& info = F::getInfo(name);
  // Normal factory construction configures defaults. Raw construction is an
  // explicit diagnostic only: some upstream destructors require configure().
  // Both paths remain isolated by the inventory runner.
  std::unique_ptr<std::remove_pointer_t<decltype(info.create())>> algorithm{
      raw ? info.create() : F::create(name)};
  if(raw)
  {
    algorithm->setName(name);
    algorithm->declareParameters();
  }

  QJsonArray inputs, outputs, parameters;
  for(const auto& [key, port] : algorithm->inputs())
    inputs.append(QJsonObject{{"name", text(key)}, {"type", text(essentia::nameOfType(*port))},
                              {"description", text(algorithm->inputDescription[key])}});
  for(const auto& [key, port] : algorithm->outputs())
    outputs.append(QJsonObject{{"name", text(key)}, {"type", text(essentia::nameOfType(*port))},
                               {"description", text(algorithm->outputDescription[key])}});
  for(const auto& [key, parameter] : algorithm->defaultParameters())
  {
    QJsonObject entry{{"name", text(key)}, {"type", int(parameter.type())},
                      {"required", !parameter.isConfigured()},
                      {"range", text(algorithm->parameterRange[key])},
                      {"description", text(algorithm->parameterDescription[key])}};
    if(parameter.isConfigured())
      entry.insert("default", text(parameter.toString()));
    parameters.append(entry);
  }
  return {{"name", text(name)}, {"category", text(info.category)},
          {"inputs", inputs}, {"outputs", outputs}, {"parameters", parameters}};
}

std::unique_ptr<Algorithm> make(const std::string& name, const essentia::ParameterMap& params = {})
{
  const auto names = Factory::keys();
  require(std::find(names.begin(), names.end(), name) != names.end(), "Unknown algorithm");
  std::unique_ptr<Algorithm> algorithm{Factory::getInfo(name).create()};
  algorithm->setName(name);
  algorithm->declareParameters();
  algorithm->configure(params);
  return algorithm;
}

QJsonObject smoke(double rate, int size)
{
  std::vector<Real> input(size), windowed(size), spectrum(size / 2 + 1);
  std::vector<Real> bands(40), coefficients(13);
  Real rms{}, pitch{}, confidence{};
  double squareSum{};
  for(int i = 0; i < size; ++i)
  {
    input[i] = static_cast<Real>(0.5 * std::sin(2. * std::numbers::pi * 440. * i / rate));
    squareSum += double(input[i]) * input[i];
  }

  auto rmsAlgo = make("RMS");
  rmsAlgo->input("array").set(input);
  rmsAlgo->output("rms").set(rms);
  rmsAlgo->compute();
  require(std::abs(rms - std::sqrt(squareSum / size)) < 1e-6, "RMS numerical mismatch");

  essentia::ParameterMap windowParams;
  windowParams.add("size", size);
  windowParams.add("type", "hann");
  auto window = make("Windowing", windowParams);
  window->input("frame").set(input);
  window->output("frame").set(windowed);

  essentia::ParameterMap spectrumParams;
  spectrumParams.add("size", size);
  auto fft = make("Spectrum", spectrumParams);
  fft->input("frame").set(windowed);
  fft->output("spectrum").set(spectrum);

  essentia::ParameterMap mfccParams;
  mfccParams.add("sampleRate", rate);
  mfccParams.add("inputSize", size / 2 + 1);
  auto mfcc = make("MFCC", mfccParams);
  mfcc->input("spectrum").set(spectrum);
  mfcc->output("bands").set(bands);
  mfcc->output("mfcc").set(coefficients);

  essentia::ParameterMap pitchParams;
  pitchParams.add("sampleRate", rate);
  pitchParams.add("frameSize", size);
  pitchParams.add("maxFrequency", rate / 2.);
  auto yin = make("PitchYinFFT", pitchParams);
  yin->input("spectrum").set(spectrum);
  yin->output("pitch").set(pitch);
  yin->output("pitchConfidence").set(confidence);

  const auto compute = [&] {
    window->compute();
    fft->compute();
    mfcc->compute();
    yin->compute();
  };
  compute();
  require(spectrum.size() == std::size_t(size / 2 + 1), "Spectrum size mismatch");
  const auto peak = std::max_element(spectrum.begin(), spectrum.end()) - spectrum.begin();
  require(std::abs(double(peak) - 440. * size / rate) <= 1., "Spectrum peak mismatch");
  require(bands.size() == 40 && coefficients.size() == 13, "MFCC dimensions mismatch");
  require(std::all_of(coefficients.begin(), coefficients.end(), [](Real v) { return std::isfinite(v); }),
          "MFCC produced a non-finite value");
  require(std::abs(pitch - 440.) < 10. && confidence > 0.8, "Pitch estimate mismatch");

  // Informational only: warm compute average, not a callback deadline/allocation proof.
  constexpr int iterations = 256;
  const auto start = std::chrono::steady_clock::now();
  for(int i = 0; i < iterations; ++i)
    compute();
  const double micros = std::chrono::duration<double, std::micro>(
                            std::chrono::steady_clock::now() - start).count() / iterations;

  bool rejectedType = false;
  std::vector<double> wrongType(size);
  try { rmsAlgo->input("array").set(wrongType); }
  catch(const essentia::EssentiaException&) { rejectedType = true; }
  require(rejectedType, "Double buffer unexpectedly accepted as vector<Real>");

  bool rejectedRange = false;
  essentia::ParameterMap invalid;
  invalid.add("numberCoefficients", 0);
  try { mfcc->configure(invalid); }
  catch(const essentia::EssentiaException&) { rejectedRange = true; }
  require(rejectedRange, "Invalid MFCC parameter unexpectedly accepted");

  bool rejectedEmpty = false;
  input.clear();
  try { rmsAlgo->compute(); }
  catch(const essentia::EssentiaException&) { rejectedEmpty = true; }
  require(rejectedEmpty, "Empty RMS frame unexpectedly accepted");

  return {{"sampleRate", rate}, {"frameSize", size}, {"rms", rms},
          {"pitchHz", pitch}, {"pitchConfidence", confidence},
          {"warmPipelineMeanMicroseconds", micros}, {"negativeChecks", "passed"}};
}

QJsonObject run(int argc, char** argv)
{
  const std::string_view command = argc > 1 ? argv[1] : "--smoke";
  if(command == "--list")
    return {{"version", ESSENTIA_VERSION}, {"revision", ESSENTIA_GIT_SHA},
            {"standard", listAlgorithms<Factory>()},
            {"streaming", listAlgorithms<essentia::streaming::AlgorithmFactory>()}};
  if((command == "--describe" || command == "--describe-raw") && argc == 4)
  {
    if(std::string_view{argv[2]} == "standard")
      return describe<Factory>(argv[3], command == "--describe-raw");
    if(std::string_view{argv[2]} == "streaming")
      return describe<essentia::streaming::AlgorithmFactory>(argv[3], command == "--describe-raw");
  }
  if(command == "--smoke")
    return {{"checks", QJsonArray{smoke(44100., 2048), smoke(48000., 2048), smoke(96000., 4096)}},
            {"status", "passed"}};
  throw std::runtime_error("Usage: --list | --describe standard|streaming NAME | --describe-raw standard|streaming NAME | --smoke");
}
}

int main(int argc, char** argv)
{
  try
  {
    essentia::init();
    struct Shutdown { ~Shutdown() { essentia::shutdown(); } } shutdown;
    const auto result = QJsonDocument{run(argc, argv)}.toJson(QJsonDocument::Compact);
    std::cout << result.constData() << '\n';
    return 0;
  }
  catch(const std::exception& error)
  {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
