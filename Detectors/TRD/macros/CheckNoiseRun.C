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
#include <TStyle.h>
#include <RDataFrame.h>

#include <map>
#include <string>
#include <fairlogger/Logger.h>
#include "TRDBase/Calibrations.h"
#include "DataFormatsTRD/HelperMethods.h"

#endif

using namespace ROOT;


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
                            {"Good", kGreen + 1, 9.0, 10.2, 0.7, 1.8},
                            {"LowNoise", kRed, 9.0, 10.2, 0.0, 0.7},
                            {"Noisy", kRed, 9.0, 10.2, 1.8, 5.0},
                            {"HighMeanRMS", kRed, 8.0, 25.0, 6.0, 30.0},
                            {"HighMean", kRed, 10.5, 25.0, 2.0, 6.0},
                            {"HighBaseline", kRed, 10.5, 520.0, 0.0, 10.0},
                            {"VeryHighNoise", kRed, 0.0, 200.0, 30.0, 180.0},
                            {"Ugly1", kRed, 200., 350., 15.0, 45.0},
                            {"Ugly2", kRed, 200., 350., 45.0, 70.0},
                            {"Ugly3", kRed, 350., 550., 10.0, 25.0},
                            {"Ugly4", kRed, 350., 550., 25.0, 60.0}}) : mClasses(classes)
  {
  }

  // the actual classification method
  int classify(o2::trd::ChannelInfo& c) {return classify(c.getEntries(), c.getMean(), c.getRMS());}
  int classify(uint64_t nentries, float mean, float rms);

  // methods to display classification criteria and results
  void drawClasses(TH2* h);
  ROOT::RDF::TH1DModel getHistogramModel();
  TH1* prepareHistogram(TH1* hist);
  TH1* createHistogram();

  ChannelStatusClass& getClassInfo(size_t i) { return mClasses[i]; }
  size_t getNClasses() { return mClasses.size(); }

  protected: 
    vector<ChannelStatusClass> mClasses;
};

void DrawChannelClass(ChannelStatusClassifier::ChannelStatusClass c, int color)
{
  TBox box;
  box.SetFillStyle(0);
  box.SetLineColor(color);
  box.DrawBox(c.minMean, c.minRMS, c.maxMean, c.maxRMS);

  TText txt;
  txt.SetTextAlign(13);
  txt.SetTextSize(0.04);
  txt.SetTextColor(color);
  txt.DrawText(c.minMean, c.maxRMS, c.label);
}

void CheckNoiseRun(const o2::trd::ChannelInfoContainer* calobject);
TTree* MakeChannelInfoTree(const o2::trd::ChannelInfoContainer* calobject);
TCanvas* MakeRunSummary(ROOT::RDF::RNode df, ChannelStatusClassifier& classifier);
TCanvas* MakeRunSummaryOld(const o2::trd::ChannelInfoContainer* calobject);

template <typename RDF>
TCanvas* MakeClassSummary(RDF df, ChannelStatusClassifier::ChannelStatusClass cls);



// void CheckNoiseRun(const long timestamp = 1679064465077)
o2::trd::ChannelInfoContainer* LoadNoiseCalObject(const long timestamp)
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

  // CheckNoiseRun(calobject);
  return calobject;
}


// void CheckNoiseRun(const TString filename = "~/Downloads/o2-trd-ChannelInfoContainer_1680185087230.root")
o2::trd::ChannelInfoContainer* LoadNoiseCalObject(const TString filename)
{
  TFile* infile = new TFile(filename);
  o2::trd::ChannelInfoContainer* calobject;
  infile->GetObject("ccdb_object", calobject);
  return calobject;
}


// void CheckNoiseRun(const o2::trd::ChannelInfoContainer* calobject)
template <typename T>
ROOT::RDF::RNode BuildNoiseDF(T x)
{
  
  // cout << "Creating RDataFrame" << endl;
  auto df1 = ROOT::RDataFrame(o2::trd::constants::NCHANNELSTOTAL)
    .Define("sector", "rdfentry_ / o2::trd::constants::NCHANNELSPERSECTOR")
    .Define("layer", "(rdfentry_ % o2::trd::constants::NCHANNELSPERSECTOR) / o2::trd::constants::NCHANNELSPERLAYER")
    .Define("row", "(rdfentry_ % o2::trd::constants::NCHANNELSPERLAYER) / o2::trd::constants::NCHANNELSPERROW")
    .Define("col", "rdfentry_ % o2::trd::constants::NCHANNELSPERROW");

  auto calobject = LoadNoiseCalObject(x);

  auto df2 = df1
               .Define("nentries", [calobject](ULong64_t i) { return calobject->getChannel(i).getEntries(); }, {"rdfentry_"})
               .Define("mean", [calobject](ULong64_t i) { return calobject->getChannel(i).getMean(); }, {"rdfentry_"})
               .Define("rms", [calobject](ULong64_t i) { return calobject->getChannel(i).getRMS(); }, {"rdfentry_"});

  return df2;
}

void CheckNoiseRun(bool save_pdf = false)
{
  auto df1 = BuildNoiseDF(1681210184624);
  // auto df1 = BuildNoiseDF("~/Downloads/o2-trd-ChannelInfoContainer_1680185087230.root");

  ChannelStatusClassifier classifier;
  auto df = df1
    .Define("class", [&classifier](uint32_t n, float mean, float rms) { return classifier.classify(n, mean, rms); }, {"nentries", "mean", "rms"});


  // Plotting
  gStyle->SetLabelSize(0.02, "X");
  gStyle->SetLabelSize(0.02, "Y");
  gStyle->SetTitleSize(0.02, "X");
  gStyle->SetTitleSize(0.02, "Y");

  TCanvas* cnv;

  cnv = MakeRunSummary(df, classifier);
  if (save_pdf) 
    cnv->SaveAs(".pdf");

  // cout << "Display summary" << endl;
  for (size_t i = 0; i < classifier.getNClasses(); i++) {
    cnv = MakeClassSummary(df.Filter(Form("class==%zu", i)), classifier.getClassInfo(i));
    if (save_pdf)
      cnv->SaveAs(".pdf");
  }

  cnv = MakeClassSummary(df.Filter("class==-2"), {"Missing", kBlue, 0.0, 20.0, 0.0, 2.0});
  if (save_pdf)
    cnv->SaveAs(".pdf");

  cnv = MakeClassSummary(df.Filter("class==-1"), {"Masked", kBlue, 9.0, 11.0, -0.1, 0.1});
  if (save_pdf)
    cnv->SaveAs(".pdf");

  cnv = MakeClassSummary(df.Filter(Form("class==%zu", classifier.getNClasses())), {"Other", kRed, 0.0, 1024.0, 0.0, 1024.0});
  if (save_pdf)
    cnv->SaveAs(".pdf");
}

template <typename T>
T* MakePadAndDraw(Double_t xlow, Double_t ylow, Double_t xup, Double_t yup, ROOT::RDF::RResultPtr<T> h, TString opt="")
{
  gPad->GetCanvas()->cd();
  TPad* pad = new TPad(h->GetName(), h->GetTitle(), xlow, ylow, xup, yup);
  pad->Draw();
  pad->cd();

  T* hh = (T*)h->Clone();
  Double_t scale = 1. / pad->GetHNDC();
  for (auto* ax : {hh->GetXaxis(), hh->GetYaxis()}) {
    ax->SetLabelSize(scale * ax->GetLabelSize());
    ax->SetTitleSize(scale * ax->GetTitleSize());
  }

  hh->Draw(opt);
  pad->Update();
  return hh;
}

void SetStyleMeanRms1D()
{
  gStyle->SetOptStat(1);
  gStyle->SetOptLogx(0);
  gStyle->SetOptLogy(1);
  gStyle->SetPadRightMargin(0.05);
  gStyle->SetPadLeftMargin(0.15);
  gStyle->SetPadTopMargin(0.02);
  gStyle->SetPadBottomMargin(0.15);
}

void SetStyleMeanVsRms()
{
  gStyle->SetOptStat(0);
  gStyle->SetOptLogx(0);
  gStyle->SetOptLogy(0);
  gStyle->SetPadRightMargin(0.1);
  gStyle->SetPadLeftMargin(0.15);
  gStyle->SetPadTopMargin(0.02);
  gStyle->SetPadBottomMargin(0.15);
}

void SetStyleClassStats()
{
  gStyle->SetOptStat(0);
  gStyle->SetOptLogx(1);
  gStyle->SetOptLogy(0);
  gStyle->SetPadRightMargin(0.1);
  gStyle->SetPadLeftMargin(0.2);
  gStyle->SetPadTopMargin(0.02);
  gStyle->SetPadBottomMargin(0.15);
}

void SetStyleMap()
{
  gStyle->SetOptStat(0);
  gStyle->SetOptLogx(0);
  gStyle->SetOptLogy(0);
  gStyle->SetPadRightMargin(0.1);
  gStyle->SetPadLeftMargin(0.1);
  gStyle->SetPadTopMargin(0.02);
  gStyle->SetPadBottomMargin(0.15);
}

template<typename RDF>
TCanvas* MakeClassSummary(RDF df, ChannelStatusClassifier::ChannelStatusClass cls)
{
  // auto mydf = df.Define("sm", "det/30").Define("ly", "det%6");

  TString id = TString("-") + cls.label;
  auto frame = new TH1F("frame"+id, "frame", 10, 0., 5.);
  auto hMean = df.Histo1D({"Mean" + id, ";Mean;# channels", 100, cls.minMean, cls.maxMean}, "mean");
  auto hRMS = df.Histo1D({"RMS" + id, ";RMS;# channels", 100, cls.minRMS, cls.maxRMS}, "rms");
  auto hMeanRMS = df.Histo2D({"hMeanRMS" + id, ";Mean;RMS", 100, cls.minMean, cls.maxMean, 100, cls.minRMS, cls.maxRMS}, "mean", "rms");
  auto hClass = df.Histo1D("class");

  auto hGlobalPos = df.Histo2D({"GlobalPos" + id, ";Sector;Layer", 18, -0.5, 17.5, 6, -0.5, 5.5}, "sector", "layer");
  auto hLayerPos = df.Histo2D({"LayerPos" + id, ";Pad row;ADC channel column", 76, -0.5, 75.5, 168, -0.5, 167.5}, "row", "col");


  auto cnv = new TCanvas("ClassSummary-"+cls.label, "Class Summary - " + cls.label, 1500, 1000);
  TText txt;
  txt.SetTextSize(0.05);
  txt.DrawText(0.02, 0.9, cls.label);
  txt.SetTextSize(0.02);
  txt.DrawText(0.02, 0.85, Form("%.1f < mean < %.1f", cls.minMean, cls.maxMean));
  txt.DrawText(0.02, 0.80, Form("%.1f < rms < %.1f", cls.minRMS, cls.maxRMS));

  SetStyleMeanRms1D();
  MakePadAndDraw(0.7, 0.3, 1.0, 0.6, hMean);
  MakePadAndDraw(0.7, 0.0, 1.0, 0.3, hRMS);

  SetStyleMeanVsRms();
  MakePadAndDraw(0.7, 0.6, 1.0, 1.0, hMeanRMS, "colz");

  SetStyleMap();
  gStyle->SetPadRightMargin(0.05);
  MakePadAndDraw(0.2, 0.65, 0.7, 1.0, hGlobalPos, "col")->DrawClone("text,same");

  gStyle->SetPadRightMargin(0.1);
  MakePadAndDraw(0.0, 0.0, 0.7, 0.65, hLayerPos, "colz");

  return cnv;
}


int ChannelStatusClassifier::classify(uint64_t nentries, float mean, float rms)
{
  if (nentries == 0) { return -2; }
  if (mean==10.0 && rms==0.0) { return -1; }

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

    // skip the class if it spans less than 3% of the range
    if ( (c.maxMean-c.minMean) / (x2-x1) < 0.03 ) continue;
    if ( (c.maxRMS-c.minRMS) / (y2-y1) < 0.03 ) continue;

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

TH1* ChannelStatusClassifier::prepareHistogram(TH1* hist)
{
  hist->GetXaxis()->SetBinLabel(1, "Missing");
  hist->GetXaxis()->SetBinLabel(2, "Masked");
  hist->GetXaxis()->SetBinLabel(hist->GetNbinsX(), "Other");
  for (int i = 0; i < mClasses.size(); i++) {
    hist->GetXaxis()->SetBinLabel(3+i, mClasses[i].label);
  }

  hist->SetFillColor(kBlue + 1);
  hist->SetBarWidth(0.8);
  hist->SetBarOffset(0.1);
  hist->SetStats(0);

  return hist;
}

ROOT::RDF::TH1DModel ChannelStatusClassifier::getHistogramModel()
{
  int n = mClasses.size();
  return ROOT::RDF::TH1DModel("Classes", "", n + 3, -2.5, n + 0.5);
}


TCanvas* MakeRunSummary(ROOT::RDF::RNode df_all, ChannelStatusClassifier& classifier)
{

  auto df = df_all.Filter("class >= 0");

  auto hMeanRms1 = df.Histo2D({"MeanRms1", ";Mean;RMS", 100, 0.0, 1024.0, 100, 0.0, 200.}, "mean", "rms");
  auto hMeanRms2 = df.Histo2D({"MeanRms2", ";Mean;RMS", 100, 0.0, 30.0, 100, 0.0, 30.0}, "mean", "rms");
  auto hMeanRms3 = df.Histo2D({"MeanRms2", ";Mean;RMS", 100, 8.5, 11.0, 100, 0.0, 6.0}, "mean", "rms");

  auto hMean = df.Histo1D({"Mean", ";Mean;#channels", 200, 8.5, 11.0}, "mean");
  auto hRMS = df.Histo1D({"RMS", ";RMS;#channels", 200, 0.4, 3.0}, "rms");

  auto hClasses = df_all.Histo1D(classifier.getHistogramModel(), "class");

  auto maxmean = df.Max("mean");
  auto maxrms = df.Max("rms");

  hMeanRms1->GetXaxis()->SetRangeUser(0.0, *maxmean * 1.1);
  hMeanRms1->GetYaxis()->SetRangeUser(0.0, *maxrms * 1.1);


  auto cnv = new TCanvas("Noise Run Summary", "Noise Run Summary", 1500, 1000);

  SetStyleMeanRms1D();
  MakePadAndDraw(0.0, 0.5, 0.33, 1.0, hRMS);
  MakePadAndDraw(0.34, 0.5, 0.66, 1.0, hMean);

  gStyle->SetOptStat(0);
  gStyle->SetPadLeftMargin(0.2);
  SetStyleClassStats();
  auto h1 = MakePadAndDraw(0.67, 0.5, 1.00, 1.0, hClasses, "hbar");
  classifier.prepareHistogram(h1);
  gPad->SetLogx();

  SetStyleMeanVsRms();
  auto h2 = MakePadAndDraw(0.00, 0.0, 0.33, 0.5, hMeanRms3, "colz");
  DrawChannelClass(classifier.getClassInfo(0), kGreen);
  DrawChannelClass(classifier.getClassInfo(1), kRed);
  DrawChannelClass(classifier.getClassInfo(2), kRed);
  // classifier.drawClasses(h2);

  auto h3 = MakePadAndDraw(0.34, 0.0, 0.66, 0.5, hMeanRms2, "colz");
  // classifier.drawClasses(h3);
  DrawChannelClass(classifier.getClassInfo(3), kRed);
  DrawChannelClass(classifier.getClassInfo(4), kRed);

  auto h4 = MakePadAndDraw(0.67, 0.0, 1.00, 0.5, hMeanRms1, "colz");
  // classifier.drawClasses(h4);
  for(int i=5; i<classifier.getNClasses(); i++) {
    DrawChannelClass(classifier.getClassInfo(i), kRed);
  }

  return cnv;
}
