#include <OpenANN/optimization/MBSGD.h>
#include <OpenANN/optimization/Optimizable.h>
#include <OpenANN/optimization/StoppingCriteria.h>
#include <OpenANN/util/AssertionMacros.h>
#include <OpenANN/util/OpenANNException.h>
#include <OpenANN/util/EigenWrapper.h>
#include <Test/Stopwatch.h>
#include <numeric>

namespace OpenANN {

MBSGD::MBSGD(fpt learningRate, fpt momentum, int batchSize, fpt gamma,
             fpt learningRateDecay, fpt minimalLearningRate, fpt momentumGain,
             fpt maximalMomentum, fpt minGain, fpt maxGain)
  : debugLogger(Logger::NONE),
    alpha(learningRate), alphaDecay(learningRateDecay),
    minAlpha(minimalLearningRate), eta(momentum), etaGain(momentumGain),
    maxEta(maximalMomentum), batchSize(batchSize), minGain(minGain),
    maxGain(maxGain), useGain(minGain != (fpt) 1.0 || maxGain != (fpt) 1.0),
    gamma(gamma), iteration(-1)
{
  if(learningRate <= 0.0 || learningRate > 1.0)
    throw OpenANNException("Invalid learning rate, should be within (0, 1]");
  if(momentum < 0.0 || momentum >= 1.0)
    throw OpenANNException("Invalid momentum, should be within [0, 1)");
  if(batchSize < 1)
    throw OpenANNException("Invalid batch size, should be greater than 0");
  if(gamma < 0.0 || gamma > 1.0)
    throw OpenANNException("Invalid gamma, should be within [0, 1]");
  if(learningRateDecay <= 0.0 || learningRateDecay > 1.0)
    throw OpenANNException("Invalid learning rate decay, should be within (0, 1]");
  if(minimalLearningRate < 0.0 || minimalLearningRate > 1.0)
    throw OpenANNException("Invalid minimum learning rate, should be within [0, 1]");
  if(momentumGain < 0.0 || momentumGain >= 1.0)
    throw OpenANNException("Invalid momentum gain, should be within [0, 1)");
  if(maximalMomentum < 0.0 || maximalMomentum > 1.0)
    throw OpenANNException("Invalid maximal momentum, should be within [0, 1]");
}

MBSGD::~MBSGD()
{
}

void MBSGD::setOptimizable(Optimizable& opt)
{
  this->opt = &opt;
}

void MBSGD::setStopCriteria(const StoppingCriteria& stop)
{
  this->stop = stop;
}

void MBSGD::optimize()
{
  OPENANN_CHECK(opt->providesInitialization());
  while(step())
  {
    if(debugLogger.isActive())
    {
      debugLogger << "Iteration " << iteration << " finished\n"
          << "Error = " << opt->error() << "\n";
    }
  }
}

bool MBSGD::step()
{
  if(iteration < 0)
    initialize();

  rng.generateIndices<std::vector<int> >(N, randomIndices, true);
  for(int n = 0; n < N; n++)
    batchAssignment[n % batches].push_back(randomIndices[n]);
  for(int b = 0; b < batches; b++)
  {
    gradient.fill(0.0);
    for(std::list<int>::const_iterator it = batchAssignment[b].begin();
        it != batchAssignment[b].end(); it++)
      gradient += opt->gradient(*it);
    OPENANN_CHECK_MATRIX_BROKEN(gradient);
    gradient /= (fpt) batchAssignment[b].size();
    batchAssignment[b].clear();

    if(useGain)
    {
      for(int p = 0; p < P; p++)
      {
        if(momentum(p)*gradient(p) >= (fpt) 0.0)
          gains(p) += 0.05;
        else
          gains(p) *= 0.95;
        gains(p) = std::min<fpt>(maxGain, std::max<fpt>(minGain, gains(p)));
        gradient(p) *= gains(p);
      }
    }

    momentum = eta * momentum - alpha * gradient;
    OPENANN_CHECK_MATRIX_BROKEN(momentum);
    if(gamma > 0.0) // Tikhonov (L2 norm) regularization
      momentum -= alpha * gamma * parameters;
    parameters += momentum;
    OPENANN_CHECK_MATRIX_BROKEN(parameters);
    opt->setParameters(parameters);

    // Decay alpha, increase momentum
    alpha *= alphaDecay;
    alpha = std::max(alpha, minAlpha);
    OPENANN_CHECK_INF_AND_NAN(alpha);
    eta += etaGain;
    eta = std::min(eta, maxEta);
    OPENANN_CHECK_INF_AND_NAN(eta);
  }

  iteration++;
  
  if(debugLogger.isActive())
      debugLogger << "alpha = " << alpha << ", eta = " << eta << "\n";

  opt->finishedIteration();

  const bool run = (stop.maximalIterations == // Maximum iterations reached?
      StoppingCriteria::defaultValue.maximalFunctionEvaluations ||
      iteration <= stop.maximalIterations) &&
      (stop.minimalSearchSpaceStep == // Gradient too small?
      StoppingCriteria::defaultValue.minimalSearchSpaceStep ||
      momentum.norm() >= stop.minimalSearchSpaceStep);
  if(!run)
    iteration = -1;
  return run;
}

Vt MBSGD::result()
{
  return parameters;
}

std::string MBSGD::name()
{
  return "Mini-Batch Stochastic Gradient Descent";
}

void MBSGD::initialize()
{
  if(opt->providesInitialization())
      opt->initialize();

  P = opt->dimension();
  N = opt->examples();
  batches = std::max(N / batchSize, 1);
  gradient.resize(P);
  gradient.fill(0.0);
  gains.resize(P);
  gains.fill(1.0);
  parameters = opt->currentParameters();
  momentum.resize(P);
  momentum.fill(0.0);
  randomIndices.reserve(N);
  rng.generateIndices<std::vector<int> >(N, randomIndices);
  batchAssignment.resize(batches);
  iteration = 0;
}

}
