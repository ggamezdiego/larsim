////////////////////////////////////////////////////////////////////////
/// \file  ISCalculationCorrelated.cxx
/// \brief Interface to algorithm class for calculating ionization electrons
///        and scintillation photon based on simple microphysics arguments
///        to establish anticorrelation between these two quantities.
///
///        To enable this in simulation, change LArG4Parameters variable
///        in your fhicl file:
///       
///        services.LArG4Parameters.IonAndScintCalculator: "Correlated" 
///
/// \author  wforeman @ iit.edu
////////////////////////////////////////////////////////////////////////

#include "Geant4/G4LossTableManager.hh"

#include "larsim/LArG4/ISCalculationCorrelated.h"
#include "lardata/DetectorInfoServices/LArPropertiesService.h"
#include "lardata/DetectorInfoServices/DetectorPropertiesService.h"
#include "larsim/Simulation/LArG4Parameters.h"
#include "larsim/Simulation/LArVoxelCalculator.h"

#include "messagefacility/MessageLogger/MessageLogger.h"
#include "cetlib_except/exception.h"


namespace larg4{

  //----------------------------------------------------------------------------
  ISCalculationCorrelated::ISCalculationCorrelated(CLHEP::HepRandomEngine&)
  {
  }

  //----------------------------------------------------------------------------
  ISCalculationCorrelated::~ISCalculationCorrelated()
  {
  }

  //----------------------------------------------------------------------------
  void ISCalculationCorrelated::Initialize()
  {
    art::ServiceHandle<sim::LArG4Parameters const> lgpHandle;
    const detinfo::LArProperties* larp = lar::providerFrom<detinfo::LArPropertiesService>();
    const detinfo::DetectorProperties* detprop = lar::providerFrom<detinfo::DetectorPropertiesService>();

    double density        = detprop->Density(detprop->Temperature());
    fEfield               = detprop->Efield();
    fScintByParticleType  = larp->ScintByParticleType();

    // ionization work function
    fWion                 = 1./lgpHandle->GeVToElectrons() * 1e3; // MeV

    // ion+excitation work function (\todo: get from LArG4Parameters or LArProperties?)
    fWph                  = 19.5 * 1e-6; // MeV

    // \todo get scintillation yield from LArG4Parameters or LArProperties
    fScintYieldFactor  = 1.;

    // the recombination coefficient is in g/(MeVcm^2), but
    // we report energy depositions in MeV/cm, need to divide
    // Recombk from the LArG4Parameters service by the density
    // of the argon we got above.
    fRecombA             = lgpHandle->RecombA();
    fRecombk             = lgpHandle->Recombk()/density;
    fModBoxA             = lgpHandle->ModBoxA();
    fModBoxB             = lgpHandle->ModBoxB()/density;
    fUseModBoxRecomb     = lgpHandle->UseModBoxRecomb();

    // Use Birks Correction in the Scintillation process
    fEMSaturation = G4LossTableManager::Instance()->EmSaturation();

    // determine the step size using the voxel sizes
    art::ServiceHandle<sim::LArVoxelCalculator const> lvc;
    double maxsize = std::max(lvc->VoxelSizeX(), std::max(lvc->VoxelSizeY(), lvc->VoxelSizeZ())) * CLHEP::cm;

    fStepSize = 0.1 * maxsize;

    return;
  }

  //----------------------------------------------------------------------------
  // fNumIonElectrons returns a value that is not corrected for life time effects
  void ISCalculationCorrelated::Reset()
  {
    fEnergyDeposit   = 0.;
    fNumScintPhotons = 0.;
    fNumIonElectrons = 0.;

    return;
  }

  //----------------------------------------------------------------------------
  // fNumIonElectrons returns a value that is not corrected for life time effects
  void ISCalculationCorrelated::CalculateIonizationAndScintillation(const G4Step* step)
  {

    fEnergyDeposit = step->GetTotalEnergyDeposit()/CLHEP::MeV;
    
    // calculate total quanta (ions + excitons)
    double Nq = fEnergyDeposit / Wph;  

    // Get the recombination factor for this voxel - Nucl.Instrum.Meth.A523:275-286,2004
    // R = A/(1 + (dE/dx)*k)
    // dE/dx is given by the voxel energy deposition, but have to convert it to MeV/cm
    // from GeV/voxel width
    // A = 0.800 +/- 0.003
    // k = (0.097+/-0.001) g/(MeVcm^2)
    // the dx depends on the trajectory of the step
    // k should be divided by the density as our dE/dx is in MeV/cm,
    // the division is handled in the constructor when we set fRecombk
    // B.Baller: Add Modified Box recombination - ArgoNeuT result submitted to JINST

    G4ThreeVector totstep = step->GetPostStepPoint()->GetPosition();
    totstep -= step->GetPreStepPoint()->GetPosition();
    double dx     = totstep.mag()/CLHEP::cm;
    double recomb = 0.;
    double dEdx   = (dx == 0.0)? 0.0: fEnergyDeposit/dx;
    double EFieldStep = EFieldAtStep(fEfield,step);

    // Guard against spurious values of dE/dx. Note: assumes density of LAr
    if(dEdx < 1.) dEdx = 1.;

    // calculate the recombination survival fraction
    if(fUseModBoxRecomb) {
      if (dx){
	double Xi = fModBoxB * dEdx / EFieldStep;
	recomb = log(fModBoxA + Xi) / Xi;
      }
      else
	recomb = 0;
    }
    else{
      recomb = fRecombA/(1. + dEdx * fRecombk / EFieldStep);
    }

    // using this recombination, calculate number of ionization electrons
    fNumIonElectrons = ( fEnergyDeposit / Wion ) * recomb;

    // calculate scintillation photons
    fNumScintPhotons = Nq - fNumIonElectrons;

    MF_LOG_DEBUG("ISCalculationCorrelated") << " Electrons produced for " << fEnergyDeposit
				       << " MeV deposited with "     << recomb
				       << " recombination: "         << fNumIonElectrons;
    MF_LOG_DEBUG("ISCalculationCorrelated") << "number photons: " << fNumScintPhotons;

    return;
  }


}// namespace
