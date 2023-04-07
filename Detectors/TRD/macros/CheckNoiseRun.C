// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// \file CheckCCDBvalues.C
/// \brief Simple macro to check TRD CCDB

#if !defined(__CLING__) || defined(__ROOTCLING__)
#include <TFile.h>
#include <TTree.h>
#include <TH1F.h>
#include <TCanvas.h>

#include <fairlogger/Logger.h>
#include "TRDBase/Calibrations.h"
#include "DataFormatsTRD/HelperMethods.h"

#endif

using namespace ROOT;

void CheckNoiseRun(const o2::trd::ChannelInfoContainer* calobject);
TTree* MakeChannelInfoTree(const o2::trd::ChannelInfoContainer* calobject);
TCanvas* MakeRunSummary(const o2::trd::ChannelInfoContainer* calobject);
// TCanvas* MakeClassSummary(RDataFrame& df);
template <typename RDF>
TCanvas* MakeClassSummary(RDF df);

  // TCanvas* MakeClassSummary(ROOT::RDF::RInterface<ROOT::Detail::RDF::RLoopManager, void>& df);

struct channel_info_t {
  int det;
  int rob;
  int mcm;
  int channel;

  float mean;
  float rms;
  uint64_t nentries;
};

class ChannelStatusClassifier
{

 public:
  struct ChannelStatusClass {
    TString label;
    int color;
    float minMean;
    float maxMean;
    float minRMS;
    float maxRMS;
  };

  ChannelStatusClassifier(vector<ChannelStatusClass> classes = {
                            {"Good", kGreen + 1, 9.2, 10.2, 0.7, 1.5},
                            {"LowNoise", kRed, 9.2, 10.2, 0.0, 0.7},
                            {"Noisy", kRed, 9.2, 10.2, 1.5, 5.0},
                            {"VeryHighNoise", kRed, 8.0, 25.0, 6.0, 30.0},
                            {"High mean", kRed, 10.5, 25.0, 2.0, 6.0},
                            {"Ugly 1", kRed, 200., 350., 15.0, 45.0},
                            {"Ugly 2", kRed, 200., 350., 45.0, 70.0}}) : mClasses(classes)
  {
  }

  // the actual classification method
  int classify(o2::trd::ChannelInfo& c) {return classifyfct(c.getEntries(), c.getMean(), c.getRMS());}
  int classifyfct(uint64_t nentries, float mean, float rms);

  // methods to display classification criteria and results
  void drawClasses(TH2* h);
  TH1* createHistogram();

 protected:
  vector<ChannelStatusClass> mClasses;
};

// void CheckNoiseRun(const long timestamp = 1679064465077)
void CheckNoiseRun(const long timestamp)
{

  auto& ccdbmgr = o2::ccdb::BasicCCDBManager::instance();
  // ccdbmgr.clearCache();
  // ccdbmgr.setURL("http://localhost:8080");
  ccdbmgr.setURL("http://ccdb-test.cern.ch:8080");

  // auto rr = ccdbmgr.getRunDuration(runNumber);
  // // std::map<std::string, std::string> md;
  // // md["runNumber"] = std::to_string(runNumber);
  // // const auto* grp = ccdbMgr.getSpecific<o2::parameters::GRPECSObject>("GLO/Config/GRPECS", (rr.first + rr.second) / 2, md);

  // cout << "Run " << runNumber << ": from " << rr.first << " to " << rr.second << endl;

  // long timestamp = 1679064465077;
  ccdbmgr.setTimestamp(timestamp); // set which time stamp of data we want this is called per timeframe, and removes the need to call it when querying a value.

  auto calobject = ccdbmgr.get<o2::trd::ChannelInfoContainer>("TRD/Calib/ChannelStatus");

  if (!calobject) {
    LOG(fatal) << "No chamber calibrations returned from CCDB for TRD calibrations";
  }

  CheckNoiseRun(calobject);

}


void CheckNoiseRun(const TString filename = "~/Downloads/o2-trd-ChannelInfoContainer_1680185087230.root")
// void CheckNoiseRun(const TString filename)
{
  TFile* infile = new TFile(filename);
  o2::trd::ChannelInfoContainer* calobject;
  infile->GetObject("ccdb_object", calobject);
  
  CheckNoiseRun(calobject);
}

ChannelStatusClassifier classifier;

void CheckNoiseRun(const o2::trd::ChannelInfoContainer* calobject)
{

  if (calobject->getData().size() != o2::trd::constants::NCHANNELSTOTAL) {
    cerr << "ERROR: ChannelInfoContainer has incorrect length " << calobject->getData().size() << ", expected " << o2::trd::constants::NCHANNELSTOTAL << endl;
    return;
  }
  // auto outfile = new TFile("chinfo.root", "RECREATE");

  gStyle->SetLabelSize(0.02, "X");
  gStyle->SetLabelSize(0.02, "Y");
  gStyle->SetTitleSize(0.02, "X");
  gStyle->SetTitleSize(0.02, "Y");
  gStyle->SetOptStat(0);

  MakeRunSummary(calobject);
  // auto tree = MakeChannelInfoTree(calobject);
  // tree->Write();

  // ROOT::RDataFrame df(*tree);
  cout << "Creating RDataFrame" << endl;
  ROOT::RDataFrame df("chinfo", "chinfo.root");
  int (ChannelStatusClassifier::*fclassify)(uint64_t,float,float);
  fclassify = &ChannelStatusClassifier::classifyfct;

  cout << "Classifying channels" << endl;
  auto f = [](uint64_t n, float m, float r) { return classifier.classifyfct(n, m, r); };
  auto df2 = df.Define("cat", f, {"nentries", "mean", "rms"});
  // df.Define("cat", (classifier.*classify), {"nentries", "mean", "rms"});

  cout << "Display summary" << endl;
  MakeClassSummary(df2.Filter("cat==3"));

}

class MyPad : public TPad
{
public:
  MyPad(TCanvas* parent, const char* name, const char* title, Double_t x1, Double_t x2, Double_t y1, Double_t y2)
  : TPad(name, title, x1, x2, y1, y2), mParent(parent)
  {
    // create pad and cd into it
    mParent->cd();
    SetBottomMargin(0.2);
    SetLeftMargin(0.2);
    SetRightMargin(0.04);
    SetTopMargin(0.04);
    Draw();
    cd();
    Update();
  }
  TH1* Adjust(TH1* h)
  {
    // Double_t scale = GetCanvas()->GetWindowHeight() / GetBBox().fHeight;
    Double_t scale = 1./GetHNDC();
    cout << "scale: " << scale << " = " << GetCanvas()->GetWindowHeight() << " / " << GetBBox().fHeight << endl;
    for (auto* ax : {h->GetXaxis(), h->GetYaxis()})
    {
      ax->SetLabelSize(scale * ax->GetLabelSize());
      ax->SetTitleSize(scale * ax->GetTitleSize());
      cout << "label size = " << ax->GetLabelSize() << "     "
           << "title size = " << ax->GetTitleSize() << endl;
    }
    return h;
  }

  template<typename T>
  T* Prepare(ROOT::RDF::RResultPtr<T> hptr)
  {
    T* h = (T*) hptr->Clone();
    Adjust(h);
    return h;
  }

protected:
  TCanvas* mParent;
};

// TCanvas* MakeClassSummary(RDataFrame& df)
template<typename RDF>
TCanvas* MakeClassSummary(RDF df)
{
  auto mydf = df.Define("sm", "det/30").Define("ly", "det%6");

  auto frame = new TH1F("frame", "frame", 10, 0., 5.);
  auto hMean = df.Histo1D({"Mean", ";mean;# channels", 100, 8.5, 11.0}, "mean");
  auto hClass = df.Histo1D("cat");

  auto hGlobalPos = mydf
    .Histo2D({"bla", ";Sector;Layer", 18, -0.5, 17.5, 6, -0.5, 5.5}, "sm", "ly");

  auto cnv = new TCanvas("Class Summary", "Class Summary", 1500, 1000);

  MyPad* pad = new MyPad(cnv, "pad1","", 0.0, 0.0, 0.5, 0.5);
  pad->SetLogy();
  auto h = pad->Prepare(hMean);
  h->Draw();

  pad = new MyPad(cnv, "pad2", "Foo", 0.3, 0.7, 1.0, 1.0);
  pad->SetLeftMargin(0.08);
  auto hl = pad->Prepare(hGlobalPos);
  hl->GetYaxis()->SetTitleOffset(0.3);
  hl->DrawClone("col");
  hl->DrawClone("text,same");

  return cnv;
}

TTree* MakeChannelInfoTree(const o2::trd::ChannelInfoContainer* calobject)
{
  auto tree = new TTree("chinfo", "Noise information per ADC channel");
  channel_info_t ch;
  tree->Branch("chInfo", &ch);

  for (size_t i = 0; i < calobject->getData().size(); i++) {
    o2::trd::HelperMethods::getPositionFromGlobalChannelIndex(i, ch.det, ch.rob, ch.mcm, ch.channel);
    ch.mean = calobject->getChannel(i).getMean();
    ch.rms = calobject->getChannel(i).getRMS();
    ch.nentries = calobject->getChannel(i).getEntries();

    tree->Fill();
  }

  return tree;
}

int ChannelStatusClassifier::classifyfct(uint64_t nentries, float mean, float rms)
{
  if (nentries == 0) { 
    return -2; 
  }

  if (mean==10.0 && rms==0.0) { 
    return -1;
  }

  for (int i=0; i<mClasses.size(); i++) {
    if (mean > mClasses[i].minMean && mean < mClasses[i].maxMean && rms > mClasses[i].minRMS && rms < mClasses[i].maxRMS) {
        return i;
    }
  }

  return mClasses.size(); // default: class not found
}

void ChannelStatusClassifier::drawClasses(TH2* h)
{
  Double_t x1 = h->GetXaxis()->GetXmin();
  Double_t x2 = h->GetXaxis()->GetXmax();
  Double_t y1 = h->GetYaxis()->GetXmin();
  Double_t y2 = h->GetYaxis()->GetXmax();

  TBox box;
  box.SetFillStyle(0);
  TText txt;
  txt.SetTextAlign(13);
  txt.SetTextSize(0.04);

  for (auto c : mClasses)
  {
    // skip this class if it's outside the current pad
    if (c.minMean < x1 || c.maxMean > x2) continue;
    if (c.minRMS < y1 || c.maxRMS > y2) continue;

    // skip the class if it spans less than 10% of the range
    if ( (c.maxMean-c.minMean) / (x2-x1) < 0.1 ) continue;
    if ( (c.maxRMS-c.minRMS) / (y2-y1) < 0.1 ) continue;

    // draw a box and text representing the class
    box.SetLineColor(c.color);
    box.DrawBox(c.minMean, c.minRMS, c.maxMean, c.maxRMS);
    txt.SetTextColor(c.color);
    txt.DrawText(c.minMean, c.maxRMS, c.label);
  }
}

TH1* ChannelStatusClassifier::createHistogram()
{
  int n = mClasses.size();
  auto hist = new TH1F("Classes", "", n + 3, -n - 0.5, 2.5);
  hist->GetXaxis()->SetBinLabel(hist->GetNbinsX(), "Missing");
  hist->GetXaxis()->SetBinLabel(hist->GetNbinsX() - 1, "Masked");
  hist->GetXaxis()->SetBinLabel(1, "Other");
  for (int i = 0; i < mClasses.size(); i++) {
    hist->GetXaxis()->SetBinLabel(hist->GetNbinsX() - 2 - i, mClasses[i].label);
  }

  hist->SetFillColor(kBlue + 1);
  hist->SetBarWidth(0.8);
  hist->SetBarOffset(0.1);
  hist->SetStats(0);

  return hist;
}

TCanvas* MakeRunSummary(const o2::trd::ChannelInfoContainer* calobject)
{
  vector<TH2F*> hMeanRms = {
    new TH2F("MeanRms1", ";Mean;RMS", 100, 0.0, 1024.0, 100, 0.0, 200.),
    new TH2F("MeanRms2", ";Mean;RMS", 100, 0.0, 30.0, 100, 0.0, 30.0),
    new TH2F("MeanRms2", ";Mean;RMS", 100, 8.5, 11.0, 100, 0.0, 6.0)
  };

  auto hMean = new TH1F("Mean", ";Mean;#channels", 200, 8.5, 11.0);
  auto hRMS = new TH1F("RMS", ";RMS;#channels", 200, 0.4, 3.0);

  ChannelStatusClassifier classifier;
  auto hClasses = classifier.createHistogram();

  int det, rob, mcm, channel;
  float maxmean = 0.0, maxrms = 0.0;

  for (size_t i = 0; i < calobject->getData().size(); i++) {
    o2::trd::HelperMethods::getPositionFromGlobalChannelIndex(i, det, rob, mcm, channel);
    auto chinfo = calobject->getChannel(i);

    if (chinfo.getMean() > maxmean) {
      maxmean = chinfo.getMean();
    }
    if (chinfo.getRMS() > maxrms) {
      maxrms = chinfo.getRMS();
    }

    auto cls = classifier.classify(chinfo);
    hClasses->Fill(-cls);

    if (cls >= 0) {
      for (auto h : hMeanRms) {
        h->Fill(chinfo.getMean(), chinfo.getRMS());
      }
      hMean->Fill(chinfo.getMean());
      hRMS->Fill(chinfo.getRMS());
    } 
  }

  hMeanRms[0]->GetXaxis()->SetRangeUser(0.0, maxmean * 1.1);
  hMeanRms[0]->GetYaxis()->SetRangeUser(0.0, maxrms * 1.1);

  for ( auto h : hMeanRms ) {
    h->SetStats(0);
    // h->Draw("colz");
  }

  auto cnv = new TCanvas("Noise Run Summary", "Noise Run Summary", 1500, 1000);

  cnv->Divide(3, 2);

  cnv->cd(1);
  gPad->SetLogy();
  hRMS->Draw();

  cnv->cd(2);
  gPad->SetLogy();
  hMean->Draw();

  cnv->cd(3);
  gPad->SetLogx();
  gPad->SetLeftMargin(0.15);
  hClasses->Draw("hbar");
  // hClasses->Draw("text");

  cnv->cd(4);
  gPad->SetRightMargin(0.15);
  hMeanRms[2]->Draw("colz");
  classifier.drawClasses(hMeanRms[2]);

  cnv->cd(5);
  gPad->SetRightMargin(0.15);
  hMeanRms[1]->Draw("colz");
  classifier.drawClasses(hMeanRms[1]);

  cnv->cd(6);
  gPad->SetRightMargin(0.15);
  hMeanRms[0]->Draw("colz");
  classifier.drawClasses(hMeanRms[0]);

  return cnv;
}

