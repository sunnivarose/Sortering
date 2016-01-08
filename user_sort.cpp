#include "Event.h"
#include "Histogram1D.h"
#include "Histogram2D.h"
#include "IOPrintf.h"
#include "OfflineSorting.h"
#include "Parameters.h"
#include "ParticleRange.h"
#include "SiriusRoutine.h"

#include <iostream>
#include <cmath>
#include <cstdlib>
#include <sstream>

#define NDEBUG 1
#include "debug.h"

//! must be 0 to disable making DE:E matrices for each strip of all detectors
#define MAKE_INDIVIDUAL_E_DE_PLOTS 1

//! must be 0 to disable making time evolution plots
#define MAKE_TIME_EVOLUTION_PLOTS 1

//! must be 0 to disable making CACTUS time-energy plots
#define MAKE_CACTUS_TIME_ENERGY_PLOTS 1

//! must be 0 to disable checking particles
#define APPLY_PARTICLE_GATE 1

// ########################################################################
// Sorting 233U(d,p) data
// version 13 april 2015 
//Sunniva Rose (fra Siem 24 august 2014)
// fission detectors in the last 4 NaI channels 
// alfna matrix are not background subtracted in this version, only gated
// on time spectra. tried to comment in OBS everywhere I changed things.
//! User sorting routine.
//
//
//
//Setter "ROSEEDIT" der jeg gjør forandringer - 130415
//
class UserXY : public SiriusRoutine {
public:
    UserXY();
    bool Sort(const Event& event);
    void CreateSpectra();
    bool Command(const std::string& cmd);
    
private:
    Histogram2Dp m_back, m_front, m_e_de_strip[8], m_e_de, m_e_de_thick, m_e_de_fiss, m_e_de_nofiss;
#if defined(MAKE_INDIVIDUAL_E_DE_PLOTS) && (MAKE_INDIVIDUAL_E_DE_PLOTS>0)
    Histogram2Dp m_e_de_individual[8][8];
    Histogram1Dp h_ede_individual[8][8];
    //8 ganger 8 arrays fordi det er 8 gange 8 partikkeltellere i SiRi (?)
#endif /* MAKE_INDIVIDUAL_E_DE_PLOTS */
    Histogram2Dp m_nai_t, m_nai_e;
    //'i' i nai står for nummer eller id (altså IKKE iodid, som man jo kunne tenke seg). 'm' er for matrise.
    //m_nai_t er dermed en matrise med tid på x-aksen og NaI-detektornummer på y-aksen
    //m_nai_e er matrise med gammaenergi på x-aksen og NaI-detektornummer på y-aksen
    Histogram2Dp m_alfna, m_alfna_bg;
    //ALFNA og ALFNABAKGRUNN defineres
    Histogram2Dp m_alfna_bg_nofiss, m_alfna_bg_fiss, m_alfna_fiss, m_alfna_nofiss;
    Histogram1Dp h_na_n, h_thick, h_ede, h_ede_r[8], h_ex, h_ex_r[8], h_particlecount;
    
#if defined(MAKE_CACTUS_TIME_ENERGY_PLOTS) && (MAKE_CACTUS_TIME_ENERGY_PLOTS>0)
    Histogram2Dp m_nai_e_t[28], m_nai_e_t_all, m_nai_e_t_c, m_siri_e_t[8], m_siri_e_t_all;
#endif /* MAKE_CACTUS_TIME_ENERGY_PLOTS */
    
#if defined(MAKE_TIME_EVOLUTION_PLOTS) && (MAKE_TIME_EVOLUTION_PLOTS>0)
    Histogram2Dp m_nai_t_evol[28], m_nai_e_evol[28];
    Histogram2Dp m_e_evol[8], m_de_evol[8][8], m_ede_evol[8], m_ex_evol;
#endif /* MAKE_TIME_EVOLUTION_PLOTS */
    
private:
    //! Correction of CACTUS time for CACTUS energy.
    Parameter tnai_corr_enai;
    
    //! Correction of CACTUS time for SiRi back detector energy.
    Parameter tnai_corr_esi;
    
    //! Polynomials to calculate excitation energy from SiRi energy (E+DE).
    /*! Contains 8*3 coefficients. */
    Parameter ex_from_ede;
    
    //! Polynomials to make an empirical correction to the calculated excitation energy.
    /*! Contains 8*2 coefficients. */
    Parameter ex_corr_exp;
    
    //! Two rectangles to cut away SiRi noise/electrons.
    /*! Contains E-minimum 1, DE-minimum 1, E-minimum 2, DE-minimum 2. */
    Parameter ede_rect;
    
    //! Thickness centroid and second-order polynomial for SiRi E dependent thickness gate width.
    /*! Contains centroid, constant width, E-scaled width. */
    Parameter thick_range;
    
    //! The particle range data from zrange.
    ParticleRange particlerange;
    
    //! Apply energy corrections to CACTUS time.
    /*! \return corrected CACTUS time. */
    float tNaI(float t,    /*!< Uncorrected CACTUS time. */
               float Enai, /*!< Calibrated CACTUS energy in keV. */
               float Esi   /*!< Calibrated SiRi back energy in keV. */);
    
    float range(float E /*!< particle energy in keV */)
    { return particlerange.GetRange( (int)E ); }
};

// ########################################################################

bool UserXY::Command(const std::string& cmd)
{
    std::istringstream icmd(cmd.c_str());
    
    std::string name;
    icmd >> name;
    
    if( name == "rangefile" ) {
        std::string filename;
        icmd >> filename;
        particlerange.Read( filename );
        return true;
    }
    return SiriusRoutine::Command(cmd);
}

// ########################################################################

UserXY::UserXY()
: tnai_corr_enai ( GetParameters(), "tnai_corr_enai", 4 )
, tnai_corr_esi  ( GetParameters(), "tnai_corr_esi", 4 )
, ex_from_ede    ( GetParameters(), "ex_from_ede", 8*3 )
, ex_corr_exp    ( GetParameters(), "ex_corr_exp", 8*2 )
, ede_rect       ( GetParameters(), "ede_rect", 4 )
, thick_range    ( GetParameters(), "thick_range", 3 )
{
    ede_rect.Set( "500 250 30 500" );
    thick_range.Set( "130  13 0" );
}


// ########################################################################

void UserXY::CreateSpectra()
{
    const int max_e = 17000, max_de = 6000;
    //Changed the maximum energy (x axis) and maximum delta energy (y axis) into something sensible for this plot
    //Don´t have to zoome like crazy everytime I make a particle spectrum :)
    m_back = Mat( "m_back", "back detector energies",
                 2000, 0, max_e, "E(Si) [keV]", 8, 0, 8, "detector nr." );
    m_front = Mat( "m_front", "front detector energies",
                  2000, 0, max_de, "#DeltaE(Si) [keV]", 64, 0, 64, "detector nr." );
    
#if defined(MAKE_INDIVIDUAL_E_DE_PLOTS) && (MAKE_INDIVIDUAL_E_DE_PLOTS>0)
    // try to change bin counts if not enough statistics for calibration
    for(int b=0; b<8; ++b ) {
        for(int f=0; f<8; ++f ) {
            m_e_de_individual[b][f] =
            Mat( ioprintf("m_e_de_b%df%d", b, f), ioprintf("#DeltaE : E detector %d strip %d", b, f),
                2000, 0, max_e, "E(Si) [keV]", 2000, 0, max_de, "#DeltaE(Si) [keV]" );
            h_ede_individual[b][f] =
            Spec( ioprintf("h_ede_b%df%d", b, f), ioprintf("E+#DeltaE detector %d strip %d", b, f),
                 2000, 0, max_e, "E+#DeltaE [keV]" );
        }
    }
#endif /* MAKE_INDIVIDUAL_E_DE_PLOTS */
    for(int f=0; f<8; ++f ) {
        m_e_de_strip[f] = Mat( ioprintf("m_e_de_f%d", f), ioprintf("E(NaI) : E(Si) strip %d", f),
                             2000, 0, max_e, "E(Si) [keV]", 2000, 0, max_de, "#DeltaE(Si) [keV]" );
    }
    
    m_e_de = Mat( "m_e_de", "#DeltaE : E for all detectors together",
                 500, 0, max_e, "E(Si) [keV]", 500, 0, max_de, "#DeltaE(Si) [keV]" );
    m_e_de_fiss = Mat( "m_e_de_fiss", "#DeltaE : E in coincidence with fission",
                 500, 0, max_e, "E(Si) [keV]", 500, 0, max_de, "#DeltaE(Si) [keV]" );
    m_e_de_nofiss = Mat( "m_e_de_nofiss", "#DeltaE : E veto for fission",
                 500, 0, max_e, "E(Si) [keV]", 500, 0, max_de, "#DeltaE(Si) [keV]" );
    m_e_de_thick = Mat( "m_e_de_thick", "#DeltaE : E for all detectors together, gated on thickness",
                       500, 0, max_e, "E(Si) [keV]", 500, 0, max_de, "#DeltaE(Si) [keV]" );
    
    m_nai_t = Mat( "m_nai_t", "t(NaI) matrix", 500, 0,  500, "? [a.u.]",     32,0,32, "det. id.");
    //hva betyr "? a.u.?"
    m_nai_e = Mat( "m_nai_e", "E(NaI) matrix", 1000, 0, 15000, "E(NaI) [keV]", 32,0,32, "det. id.");
    //m_nai_e går fra E=0 til E 15000 keV, spesifikt, og ikke til max energi (variabel)
    //det første tallet er det som oppgir antall kanaler eller bins, dette må(?) være et antall som går opp i energien
    
    
    //Under defineres de forskjellige matrisene, hva de skal hete, bin-størresel, hva det skal stå på aksene osv
    m_alfna_bg = Mat( "m_alfna_bg", "E(NaI) : E_{x} background",
                          2000, -2000, 14000, "E(NaI) [keV]", 2000, -2000, 14000, "E_{x} [keV]" );
    m_alfna_bg_fiss = Mat( "m_alfna_bg_fiss", "E(NaI) : E_{x} background with fission",
                  2000, -2000, 14000, "E(NaI) [keV]", 2000, -2000, 14000, "E_{x} [keV]" );
    m_alfna_bg_nofiss = Mat( "m_alfna_bg_nofiss", "E(NaI) : E_{x} background without fission",
                     2000, -2000, 14000, "E(NaI) [keV]", 2000, -2000, 14000, "E_{x} [keV]" );
    /*
     * m_alfna = Mat( "m_alfna", "E(NaI) : E_{x}",
                       2000, -2000, 14000, "E(NaI) [keV]", 2000, -2000, 14000, "E_{x} [keV]" );
                       */ //Originalt slik ALFNA-matrisen defineres, mine forandringer under. 160415
    m_alfna = Mat( "m_alfna", "E(NaI) : E_{x} alfna",
                       2000, -2000, 14000, "E(NaI) [keV]", 2000, -2000, 14000, "E_{x} [keV]" );
    //x-aksen angis først: ant bin(?),Emin,Emax, tittel på aksen , så y-aksen
    m_alfna_fiss = Mat( "m_alfna_fiss", "E(NaI) : E_{x} in coincidence with fission",
                  2000, -2000, 14000, "E(NaI) [keV]", 2000, -500, 14000, "E_{x} [keV]" );
    m_alfna_nofiss = Mat( "m_alfna_nofiss", "E(NaI) : E_{x} veto for fission",
                          2000, -2000, 14000, "E(NaI) [keV]", 2000, -2000, 14000, "E_{x} [keV]" );


    
    h_na_n = Spec("h_na_n", "NaI multiplicity", 32, 0, 32, "multiplicity");
    
    h_thick = Spec("h_thick", "apparent #DeltaE thickness", 500, 0, 500, "#DeltaE 'thickness' [um]");
    
    h_particlecount = Spec("h_particlecount", "number of particles found", 64, 0, 64, "particle count");
    
    for(int f=0; f<8; ++f ) {
        h_ede_r[f] = Spec(ioprintf("h_ede_f%d", f), ioprintf("E+#DeltaE ring %d", f),
                          2000, 0, max_e, "E+#DeltaE [keV]");
        //h_ede_r[f]->SetLineColor(f+1);
        
        h_ex_r[f] = Spec(ioprintf("h_ex_f%d", f), ioprintf("E_{x} ring %d", f),
                         2000, -2000, 14000, "E_{x} [keV]");
        //h_ex_r[f]->SetLineColor(f+1);
    }
    h_ede = Spec("h_ede", "E+#DeltaE all detectors", 2000, 0, max_e, "E+#DeltaE [keV]");
    h_ex  = Spec("h_ex", "E_{x} all detectors", 2000, -2000, 14000, "E_{x} [keV]");
    
#if defined(MAKE_CACTUS_TIME_ENERGY_PLOTS) && (MAKE_CACTUS_TIME_ENERGY_PLOTS>0)
    const int max_enai = 12000;
    //maximum energy of the gammadetectors (x axis) is 12000 keV
    //HOWEVER, this maximum value is not the same for all plots - for some it has been sat individually (not as a variable)
    for(int n=0; n<28; ++n ) {
        m_nai_e_t[n] = Mat( ioprintf("m_nai_e_t_%02d", n), ioprintf("t : E NaI %d", n),
                           500, 0, max_enai, "E(NaI) [keV]", 500, 0, 500, "t(NaI) [a.u.]" );
    }
    m_nai_e_t_all = Mat( "m_nai_e_t", "t : E NaI all together",
                        500, 0, max_enai, "E(NaI) [keV]", 500, 0, 500, "t(NaI) [a.u.]" );
    m_nai_e_t_c   = Mat( "m_nai_e_t_c", "t : E NaI all together, corrected",
                        500, 0, max_enai, "E(NaI) [keV]", 500, 0, 500, "t(NaI) [a.u.]" );
    
    for(int n=0; n<8; ++n ) {
        m_siri_e_t[n] = Mat( ioprintf("m_siri_e_t_b%d", n), ioprintf("t(NaI) : E(Si) detector %d", n),
                            500, 0, max_e, "E(Si) [keV]", 500, 0, 500, "t(NaI) corr. [a.u.]" );
    }
    m_siri_e_t_all = Mat( "m_siri_e_t", "t(NaI) : E(Si) all detectors",
                         500, 0, max_e, "E(Si) [keV]", 500, 0, 500, "t(NaI) corr. [a.u.]" );
#endif /* MAKE_CACTUS_TIME_ENERGY_PLOTS */
    
#if defined(MAKE_TIME_EVOLUTION_PLOTS) && (MAKE_TIME_EVOLUTION_PLOTS>0)
    // time evolution plots
    const int MT = 4*24*3600;
    const int NT = 4*24;
    for(int n=0; n<28; ++n ) {
        m_nai_t_evol[n] = Mat( ioprintf("m_nai_t_evol_%02d", n), ioprintf("time : t NaI %d", n),
                              500, 0, 500, "t(NaI) [a.u.]", NT, 0, MT, "wall clock time [s]" );
        m_nai_e_evol[n] = Mat( ioprintf("m_nai_e_evol_%02d", n), ioprintf("time : e NaI %d", n),
                              500, -1000, max_enai-1000, "e(NaI) [keV]", NT, 0, MT, "wall clock time [s]" );
    }
    
    for(int b=0; b<8; ++b ) {
        m_e_evol[b] = Mat( ioprintf("m_e_evol_b%d", b), ioprintf("time : E detector %d", b),
                          500, 0, max_e, "E(Si) [keV]", NT, 0, MT, "wall clock time [s]" );
        for(int f=0; f<8; ++f ) {
            m_de_evol[b][f] =
            Mat( ioprintf("m_de_evol_b%df%d", b, f), ioprintf("time : #DeltaE detector %d strip %d", b, f),
                500, 0, max_de, "#DeltaE(Si) [keV]", NT, 0, MT, "wall clock time [s]" );
        }
        m_ede_evol[b] = Mat( ioprintf("m_ede_evol_f%d", b), ioprintf("time : E+#DeltaE ring %d", b),
                            500, 0, max_e, "E+#DeltaE(Si) [keV]", NT, 0, MT, "wall clock time [s]" );
    }
    m_ex_evol  = Mat("m_ex_evol", "time : E_{x} all detectors", 800, -2000, 14000, "E_{x} [keV]",
                     NT, 0, MT, "wall clock time [s]" );
#endif /* MAKE_TIME_EVOLUTION_PLOTS */
}

// ########################################################################

static float _rando = 0;
static float calib(unsigned int raw, float gain, float shift)
{
    return shift + (raw+_rando) * gain;
}

// ########################################################################

float UserXY::tNaI(float t, float Enai, float Esi)
{
    const float c = tnai_corr_enai[0] + tnai_corr_enai[1]/(Enai+tnai_corr_enai[2]) + tnai_corr_enai[3]*Enai;
    const float d = tnai_corr_esi [0] + tnai_corr_esi [1]/(Esi +tnai_corr_esi [2]) + tnai_corr_esi [3]*Esi;
    return t - c - d;
}

// ########################################################################

bool UserXY::Sort(const Event& event)
{
    _rando = drand48() - 0.5;
    
    // ..................................................
    
    const unsigned int e_mini = (unsigned int)std::min(ede_rect[0], ede_rect[2]);
    unsigned int si_e_raw[8];
    for( int i=0; i<event.n_e; i++ ) {
        int id = event.e[i].chn;
        if( !(id&1) || id>= 16 )
            continue; // ignore guard rings
        
        id >>= 1; // detector number 0..7
        
        // only keep raw E here, we don't know which front detector
        // has fired, so we also don't know which coefficients to use
        // to calibrate the back detector
        const unsigned int raw = event.e[i].adc;
        if( raw >= e_mini )
            si_e_raw[id] = raw;
        else
            si_e_raw[id] = 0;
        
        // approximate calibration
        m_back->Fill( (int)calib( raw, gain_e[8*id], shift_e[8*id] ), id );
    }
    
    // ..................................................
    
    int si_goodcount = 0;
    int dei=0, ei=0; // ei and dei are such valid even if no particle is found
    float de = 0;
    
    // calibrate dE detectors, reject event if more than one over threshold
    for( int i=0; i<event.n_de; i++ ) {
        const int id   = event.de[i].chn;
        const int id_b = id / 8;
        const int id_f = id % 8;
        
        const unsigned int raw = event.de[i].adc;
        const float de_cal = calib( raw, gain_de[id], shift_de[id] );
        
        m_front->Fill( (int)de_cal, id );
        
        if( si_goodcount<2 && si_e_raw[id_b]>0 &&
           ( (si_e_raw[id_b]>ede_rect[0] && de_cal>ede_rect[1])
            || (si_e_raw[id_b]>ede_rect[2] && de_cal>ede_rect[3]) ) )
        {
            si_goodcount += 1;
            ei  = id_b;
            dei = id_f;
            de  = de_cal;
        }
    }
    h_particlecount->Fill( si_goodcount );
    
    if( APPLY_PARTICLE_GATE && si_goodcount != 1 )
        // no detector above threshold, reject event
        return true;
    
    if( ei==3 && dei==5 )
        // bad strip
        return true;
    

    
    const float e  = calib( si_e_raw[ei], gain_e[8*ei+dei], shift_e[8*ei+dei] );
    const int e_int = int(e), de_int = int(de);
    //e_int er E(nergi), men denne kommer inn som float og gjøres heretter om til en int(?)
    //de_int er deltaE(nergi), men denne kommer inn som float og gjøres heretter om til en int (?)



//****************************************************************************************************
    // investigation for fission
    int fis = 0;
    for( int j=0; j<event.n_na; j++ ) {
        
        const int idf = event.na[j].chn;
        
        if ( idf<28 )
            continue;
        
        const float na_e_f = calib( (int)event.na[j].adc, gain_na[idf], shift_na[idf] );
        
        const float na_t_f = calib( (int)event.na[j].tdc/8, gain_tna[idf], shift_tna[idf] );
        

//        if ( na_t_f>190 && na_t_f<220 && na_e_f>1195 && na_e_f<1225 ) fis = 1;
//OBS edited fission gates for 233U data
        if ( na_t_f>165 && na_t_f<185 && na_e_f>1570 && na_e_f<1700 ) fis = 1;
    }
//****************************************************************************************************
    
//****************************************************************************************************
    
    //E-dE matrix with veto for fission
    if( fis==0 ) m_e_de_nofiss->Fill( e_int, de_int );
    
    //E-dE matrix only in case of fission
    if( fis==1 ) m_e_de_fiss->Fill( e_int, de_int );
    
//****************************************************************************************************
    
    
    // make DE:E matrices
#if defined(MAKE_INDIVIDUAL_E_DE_PLOTS) && (MAKE_INDIVIDUAL_E_DE_PLOTS>0)
    m_e_de_individual[ei][dei]->Fill( e_int, de_int );
#endif /* MAKE_INDIVIDUAL_E_DE_PLOTS */
    m_e_de_strip[dei]->Fill( e_int, de_int );
    m_e_de->Fill( e_int, de_int );
    
    const float thick = range(e+de)-range(e);
    h_thick->Fill( (int)thick );
    
    const float thick_dev = thick_range[1] + thick_range[2]*e;
    const bool have_pp = fabs(thick-thick_range[0])<thick_dev;
    if( APPLY_PARTICLE_GATE && !have_pp )
        return true;
    
    m_e_de_thick->Fill( e_int, de_int );
    const float ede = e+de;
    const int   ede_int = (int)ede;
    h_ede->Fill( ede_int );
    h_ede_r[dei]->Fill( ede_int );
#if defined(MAKE_INDIVIDUAL_E_DE_PLOTS) && (MAKE_INDIVIDUAL_E_DE_PLOTS>0)
    h_ede_individual[ei][dei]->Fill( ede_int );
#endif /* MAKE_INDIVIDUAL_E_DE_PLOTS */
    
    // fit of kinz Ex(E+DE)
    const float ex_theo = ex_from_ede[3*dei+0] + (ede)*(ex_from_ede[3*dei+1] + (ede)*ex_from_ede[3*dei+2]);
    //const float ex_theo = ex_from_ede.Poly(ede, 3*dei, 3);
    
    // make experimental corrections
    const float ex = ex_corr_exp[2*dei]+ex_corr_exp[2*dei+1]*ex_theo;
    const int   ex_int = (int)ex;
    

    
    if ( fis==1 )h_ex->Fill( ex_int );
    h_ex_r[dei]->Fill( ex_int );
    
    // ..................................................
    
#if defined(MAKE_TIME_EVOLUTION_PLOTS) && (MAKE_TIME_EVOLUTION_PLOTS>0)
    const int timediff = Timediff(event);
    m_ex_evol->Fill( ex_int, timediff );
#endif /* MAKE_TIME_EVOLUTION_PLOTS */
    
    // ..................................................
    
    h_na_n->Fill(event.n_na);
    for( int i=0; i<event.n_na; i++ ) {
        const int id = event.na[i].chn;
        if( id == 15 )
            continue;



        if( event.na[i].adc <= 0 )
            continue;
        
        const float na_e = calib( (int)event.na[i].adc, gain_na[id], shift_na[id] );
        const int   na_e_int = (int)na_e;
        
        // fission signals in id 28, 29, 30, 31

        if( event.na[i].tdc <= 0 )
            continue;
        
        
        
        const float na_t = calib( (int)event.na[i].tdc/8, gain_tna[id], shift_tna[id] );
        const int   na_t_int = (int)na_t;
        const int   na_t_c = (int)tNaI(na_t, na_e, e);
        
             m_nai_t->Fill( na_t_int, id );
        m_nai_e->Fill( na_e_int, id );
        //ROSEEDIT den utkommenterte linjen over er originalen.
        //forsøker å sett en gate under
        //if( de_int>965 && de_int<1150 && e_int>9500 && e_int<10100)m_nai_e->Fill( na_e_int, id ); //<--gate testet 150415
        //if( de_int>970 && de_int<1170 && e_int>9465 && e_int<9750)m_nai_e->Fill( na_e_int, id ); //<--gate testet 160415
        //if( de_int>200 && de_int<210 && e_int>9400 && e_int<9500)m_nai_e->Fill( na_e_int, id ); <-- tight test med Therese 130415

//****************************************************************************************************
        // investigation for fission
        int fiss = 0;
        for( int j=0; j<event.n_na; j++ ) {
            const int idf = event.na[j].chn;
            if ( idf<28 )
                continue;
            
            const float na_e_f = calib( (int)event.na[j].adc, gain_na[idf], shift_na[idf] );
            
            const float na_t_f = calib( (int)event.na[j].tdc/8, gain_tna[idf], shift_tna[idf] );
            
            
//        if ( na_t_f>190 && na_t_f<220 && na_e_f>1195 && na_e_f<1225 ) fiss = 1;
//new fission gated for 233U data
                    //if ( na_t_f>165 && na_t_f<185 && na_e_f>1570 && na_e_f<1700 ) fiss = 1;
                    if ( na_t_f>160 && na_t_f<186 && na_e_f>1570 && na_e_f<1700 ) fiss = 1; //<-- ny gate 130515

        }
//****************************************************************************************************

        
#if defined(MAKE_CACTUS_TIME_ENERGY_PLOTS) && (MAKE_CACTUS_TIME_ENERGY_PLOTS>0)
        if(id<28 && fiss==0) m_nai_e_t[id] ->Fill( na_e_int,  na_t_int );
        if(id<28 && fiss==0) m_nai_e_t_all ->Fill( na_e_int,  na_t_int );
        if(id<28 && fiss==0) m_nai_e_t_c   ->Fill( na_e_int,  na_t_c );
        if(id<28 && fiss==0) m_siri_e_t[ei]->Fill( e_int, na_t_c );
        if(id<28 && fiss==0) m_siri_e_t_all->Fill( e_int, na_t_c );
#endif /* MAKE_CACTUS_TIME_ENERGY_PLOTS */
        

        int weight = 1;

        //Particle-gamma matrix all together
        //if( id<28 && na_t_c>190 && na_t_c<210 ) {  <-- slik så det ut før 210415
        //if( id<28 && na_t_c>195 && na_t_c<220 ) { //  <-- ny gate funnet ved å projisere ned på x-aksen i m_nai_t, 210415
        if( id<28 && na_t_c>195 && na_t_c<205 ) {
            m_alfna->Fill( na_e_int, ex_int, 1);
            //m_alfna_total->Fill( na_e_int, ex_int, 1);
        //} else if( id<28 && na_t_c>120 && na_t_c<145 ) {
        } else if( id<28 && na_t_c>102 && na_t_c<112 ) { // <--viktig at det trekkes fra like mange kanaler som gaten over på Fill. Ny gate 100615 etter å ha sjekket tiden i m_siri_e_t projisert på y-aksen
          //m_alfna->Fill( na_e_int, ex_int, -1); // <-- denne var kommentert ut (før 210415), når den ikke er det blir bakgrunn automatisk trukket fra i ALFNA-plott!
            m_alfna_bg->Fill( na_e_int, ex_int );
            weight = -1;
        }

        
//***************************************************************************************************
        
        //Particle-gamma matrix with veto for fission
        //if( id<28 && fiss==0 && na_t_c>190 && na_t_c<210 ) { <-- slik så det ut før 110615
        if( id<28 && fiss==0 && na_t_c>195 && na_t_c<205 ) { //lager samme gate som for vanlig alfna
                m_alfna_nofiss->Fill( na_e_int, ex_int, 1);
                //m_alfna_nofiss_total->Fill( na_e_int, ex_int, 1);
            //} else if( id<28 && fiss==0 && na_t_c>111 && na_t_c<147 ) { <-- før 110615
            } else if( id<28 && fiss==0 && na_t_c>102 && na_t_c<112 ) { //samme gate som for vanlig alfna_bg
              //m_alfna_nofiss->Fill( na_e_int, ex_int, -1);
                m_alfna_bg_nofiss->Fill( na_e_int, ex_int );
            }

        //Particle-gamma matrix only in case of fission
        //if( id<28 && fiss==1 && na_t_c>190 && na_t_c<210 ) { <-- slik så det ut før 110615
        if( id<28 && fiss==1 && na_t_c>195 && na_t_c<205 ) { //lager samme gate som for vanlig alfna
            m_alfna_fiss->Fill( na_e_int, ex_int, 1);
            //m_alfna_fiss_total->Fill( na_e_int, ex_int, 1);
        } else if( id<28 && fiss==1 && na_t_c>102 && na_t_c<112 ) { //samme gate som for vanlig alfna_bg
          //m_alfna_fiss->Fill( na_e_int, ex_int, -1);
            m_alfna_bg_fiss->Fill( na_e_int, ex_int );
        }
//****************************************************************************************************

        
        

        
#if defined(MAKE_TIME_EVOLUTION_PLOTS) && (MAKE_TIME_EVOLUTION_PLOTS>0)
        m_nai_e_evol[id]->Fill( na_e_int, timediff, weight );
        m_nai_t_evol[id]->Fill( na_t_c,   timediff );
#endif /* MAKE_TIME_EVOLUTION_PLOTS */
    }
#if defined(MAKE_TIME_EVOLUTION_PLOTS) && (MAKE_TIME_EVOLUTION_PLOTS>0)
    m_e_evol  [ei]     ->Fill( e_int,   timediff );
    m_de_evol [ei][dei]->Fill( de_int,  timediff );
    m_ede_evol[dei]    ->Fill( ede_int, timediff );
#endif /* MAKE_TIME_EVOLUTION_PLOTS */
    
    return true;
}

// ########################################################################
// ########################################################################
// ########################################################################

int main(int argc, char* argv[])
{
    return OfflineSorting::Run(new UserXY(), argc, argv );
}
