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
#include <RDataFrame.h>

#include <fairlogger/Logger.h>
#include "TRDBase/Calibrations.h"
#include "DataFormatsTRD/HelperMethods.h"

#endif

using namespace ROOT;


template <typename RDF>
TCanvas* MakeClassSummary(RDF df);



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
                            {"Good", kGreen + 1, 9.2, 10.2, 0.7, 1.8},
                            {"LowNoise", kRed, 9.2, 10.2, 0.0, 0.7},
                            {"Noisy", kRed, 9.2, 10.2, 1.8, 5.0},
                            {"VeryHighNoise", kRed, 8.0, 25.0, 6.0, 30.0},
                            {"High mean", kRed, 10.5, 25.0, 2.0, 6.0},
                            {"Ugly 1", kRed, 200., 350., 15.0, 45.0},
                            {"Ugly 2", kRed, 200., 350., 45.0, 70.0},
                            {"Ugly 3", kRed, 350., 550., 5.0, 25.0},
                            {"Ugly 4", kRed, 350., 550., 25.0, 60.0}}) : mClasses(classes)
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

 protected:
  vector<ChannelStatusClass> mClasses;
};

void CheckNoiseRun(const o2::trd::ChannelInfoContainer* calobject);
TTree* MakeChannelInfoTree(const o2::trd::ChannelInfoContainer* calobject);
TCanvas* MakeRunSummary(ROOT::RDF::RNode df, ChannelStatusClassifier& classifier);
TCanvas* MakeRunSummaryOld(const o2::trd::ChannelInfoContainer* calobject);

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
void CheckNoiseRun()
{

  gStyle->SetLabelSize(0.02, "X");
  gStyle->SetLabelSize(0.02, "Y");
  gStyle->SetTitleSize(0.02, "X");
  gStyle->SetTitleSize(0.02, "Y");
  // gStyle->SetOptStat(0);

  cout << "Creating RDataFrame" << endl;
  auto df1 = ROOT::RDataFrame(o2::trd::constants::NCHANNELSTOTAL)
               .Define("sector", "rdfentry_ / o2::trd::constants::NCHANNELSPERSECTOR")
               .Define("layer", "(rdfentry_ % o2::trd::constants::NCHANNELSPERSECTOR) / o2::trd::constants::NCHANNELSPERLAYER")
               .Define("row", "(rdfentry_ % o2::trd::constants::NCHANNELSPERLAYER) / o2::trd::constants::NCHANNELSPERROW")
               .Define("col", "rdfentry_ % o2::trd::constants::NCHANNELSPERROW");

  // auto calobject = LoadNoiseCalObject(1681210184624);
  auto calobject = LoadNoiseCalObject("~/Downloads/o2-trd-ChannelInfoContainer_1680185087230.root");

  auto df2 = df1
    .Define("nentries", [calobject](ULong64_t i) { return calobject->getChannel(i).getEntries(); }, {"rdfentry_"})
    .Define("mean", [calobject](ULong64_t i) { return calobject->getChannel(i).getMean(); }, {"rdfentry_"})
    .Define("rms", [calobject](ULong64_t i) { return calobject->getChannel(i).getRMS(); }, {"rdfentry_"});

  ChannelStatusClassifier classifier;
  auto df = df2
    .Define("class", [&classifier](uint32_t n, float mean, float rms) { return classifier.classify(n, mean, rms); }, {"nentries", "mean", "rms"});

  auto disp = df.Display({"mean","rms", "class"});
  disp->Print();

  cout << (*df.Mean("rms")) << "  " << (*df.Mean("mean")) << endl;

  MakeRunSummaryOld(calobject);
  MakeRunSummary(df, classifier);

  // cout << "Classifying channels" << endl;
  // int (ChannelStatusClassifier::*fclassify)(uint64_t, float, float);
  // fclassify = &ChannelStatusClassifier::classifyfct;

  // auto f = [](uint64_t n, float m, float r) { return classifier.classifyfct(n, m, r); };
  // auto df2 = df.Define("cat", f, {"nentries", "mean", "rms"});
  // // df.Define("cat", (classifier.*classify), {"nentries", "mean", "rms"});

  // cout << "Display summary" << endl;
  // MakeClassSummary(df2.Filter("cat==3"));

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

template <typename T>
T* MakePadAndDraw(Double_t xlow, Double_t ylow, Double_t xup, Double_t yup, ROOT::RDF::RResultPtr<T> h, TString opt="")
{
  gPad->GetCanvas()->cd();
  TPad* pad = new TPad(h->GetName(), h->GetTitle(), xlow, ylow, xup, yup);
  pad->SetBottomMargin(0.2);
  pad->SetLeftMargin(0.2);
  pad->SetRightMargin(0.04);
  pad->SetTopMargin(0.04);
  pad->Draw();
  pad->cd();
  // pad->Update();

  T* hh = (T*)h->Clone();
  Double_t scale = 1. / pad->GetHNDC();
  // cout << "scale: " << scale << " = " << pad->GetCanvas()->GetWindowHeight() << " / " << pad->GetBBox().fHeight << endl;
  for (auto* ax : {hh->GetXaxis(), hh->GetYaxis()}) {
    ax->SetLabelSize(scale * ax->GetLabelSize());
    ax->SetTitleSize(scale * ax->GetTitleSize());
    // cout << "label size = " << ax->GetLabelSize() << "     "
        //  << "title size = " << ax->GetTitleSize() << endl;
  }

  hh->Draw(opt);
  pad->Update();
  return hh;
}

template<typename RDF>
TCanvas* MakeClassSummary(RDF df)
{
  auto mydf = df.Define("sm", "det/30").Define("ly", "det%6");

  auto frame = new TH1F("frame", "frame", 10, 0., 5.);
  auto hMean = df.Histo1D({"Mean", ";mean;# channels", 100, 8.5, 11.0}, "mean");
  auto hClass = df.Histo1D("cat");

  auto hGlobalPos = mydf.Histo2D({"bla", ";Sector;Layer", 18, -0.5, 17.5, 6, -0.5, 5.5}, "sector", "layer");

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

TCanvas* MakeRunSummaryOld(const o2::trd::ChannelInfoContainer* calobject)
{
  vector<TH2F*> hMeanRms = {
    new TH2F("OldMeanRms1", ";Mean;RMS", 100, 0.0, 1024.0, 100, 0.0, 200.),
    new TH2F("OldMeanRms2", ";Mean;RMS", 100, 0.0, 30.0, 100, 0.0, 30.0),
    new TH2F("OldMeanRms3", ";Mean;RMS", 100, 8.5, 11.0, 100, 0.0, 6.0)
  };

  auto hMean = new TH1F("OldMean", ";Mean;#channels", 200, 8.5, 11.0);
  auto hRMS = new TH1F("OldRMS", ";RMS;#channels", 200, 0.4, 3.0);

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

  auto cnv = new TCanvas("Old Noise Run Summary", "Old Noise Run Summary", 1500, 1000);

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

  // cnv->Divide(3, 2);

  gStyle->SetOptStat(1);

  auto h = MakePadAndDraw(0.0, 0.5, 0.33, 1.0, hRMS);
  // h->SetStats(kTRUE);
  // h->Draw();
  gPad->SetLogy();
  // gPad->Update();

  MakePadAndDraw(0.34, 0.5, 0.66, 1.0, hMean);
  // h->SetStats(kTRUE);
  gPad->SetLogy();
  // gPad->Update();

  gStyle->SetOptStat(0);

  auto h1 = MakePadAndDraw(0.67, 0.5, 1.00, 1.0, hClasses, "hbar");
  classifier.prepareHistogram(h1);
  gPad->SetLogx();

  auto h2 = MakePadAndDraw(0.00, 0.0, 0.33, 0.5, hMeanRms3, "colz");
  classifier.drawClasses(h2);

  auto h3 = MakePadAndDraw(0.34, 0.0, 0.66, 0.5, hMeanRms2, "colz");
  classifier.drawClasses(h3);

  auto h4 = MakePadAndDraw(0.67, 0.0, 1.00, 0.5, hMeanRms1, "colz");
  classifier.drawClasses(h4);

  // cnv->cd(3);
  // // gPad->SetLogx();
  // gPad->SetLeftMargin(0.15);
  // // hClasses->Draw("hbar");
  // // hClasses->Draw("text");


  return cnv;
}

