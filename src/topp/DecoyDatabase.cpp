// Copyright (c) 2002-present, The OpenMS Team -- EKU Tuebingen, ETH Zurich, and FU Berlin
// SPDX-License-Identifier: BSD-3-Clause
//
// --------------------------------------------------------------------------
// $Maintainer: Sven Nahnsen $
// $Authors: Sven Nahnsen, Andreas Bertsch, Chris Bielow, Philipp Wang $
// --------------------------------------------------------------------------

#include <OpenMS/FORMAT/FASTAFile.h>
#include <OpenMS/METADATA/ProteinIdentification.h>
#include <OpenMS/CHEMISTRY/ProteaseDB.h>
#include <OpenMS/CHEMISTRY/ProteaseDigestion.h>
#include <OpenMS/ANALYSIS/OPENSWATH/MRMDecoy.h>
#include <OpenMS/CHEMISTRY/DigestionEnzyme.h>
#include <OpenMS/APPLICATIONS/TOPPBase.h>
#include <OpenMS/MATH/MathFunctions.h>
#include <OpenMS/DATASTRUCTURES/FASTAContainer.h>
#include <regex>
#include <OpenMS/ANALYSIS/ID/NeighborSeq.h>




using namespace OpenMS;
using namespace std;

//-------------------------------------------------------------
//Doxygen docu
//-------------------------------------------------------------

/**
@page TOPP_DecoyDatabase DecoyDatabase

@brief Create a decoy peptide database from standard FASTA databases.

Decoy databases are useful to control false discovery rates and thus estimate score cutoffs for identified spectra.

The decoy can either be generated by reversing or shuffling each of the peptides of a sequence (as defined by a given enzyme).
For reversing the N and C terminus of the peptides are kept in position by default.

To get a 'contaminants' database have a look at http://www.thegpm.org/crap/index.html or find/create your own contaminant database.

Multiple databases can be provided as input, which will internally be concatenated before being used for decoy generation.
This allows you to specify your target database plus a contaminant file and obtain a concatenated
target-decoy database using a single call, e.g., DecoyDatabase -in human.fasta crap.fasta -out human_TD.fasta

By default, a combined database is created where target and decoy sequences are written interleaved
(i.e., target1, decoy1, target2, decoy2,...).
If you need all targets before the decoys for some reason, use @p only_decoy and concatenate the files
externally.

The tool will keep track of all protein identifiers and report duplicates.

Also the tool automatically checks for decoys already in the input files (based on most common pre-/suffixes)
and terminates the program if decoys are found.

<B>The command line parameters of this tool are:</B>
@verbinclude TOPP_DecoyDatabase.cli
<B>INI file documentation of this tool:</B>
@htmlinclude TOPP_DecoyDatabase.html


The Neighbor Peptide functionality in the TOPPDecoyDatabase class is designed to find peptides from a given set of sequences (FASTA file) that are
similar to a target peptide based on mass and spectral characteristics. This section will detail the methods and functionalities specifically related
to the Neighbor Peptide search. 
NeighborSeq class has more details look at *OpenMS\src\openms\include\OpenMS\ANALYSIS\ID\NeighborSeq.h
*/

// We do not want this class to show up in the docu:
/// @cond TOPPCLASSES

class TOPPDecoyDatabase :
  public TOPPBase
{
public:
  TOPPDecoyDatabase() :
    TOPPBase("DecoyDatabase", "Create decoy sequence database from forward sequence database.")
  {
  }

protected:
  void registerOptionsAndFlags_() override
  {
    registerInputFileList_("in", "<file(s)>", ListUtils::create<String>(""), "Input FASTA file(s), each containing a database. It is recommended to include a contaminant database as well.");
    setValidFormats_("in", ListUtils::create<String>("fasta"));
    registerOutputFile_("out", "<file>", "", "Output FASTA file where the decoy database will be written to.");
    setValidFormats_("out", ListUtils::create<String>("fasta"));
    registerStringOption_("decoy_string", "<string>", "DECOY_", "String that is combined with the accession of the protein identifier to indicate a decoy protein.", false);
    registerStringOption_("decoy_string_position", "<choice>", "prefix", "Should the 'decoy_string' be prepended (prefix) or appended (suffix) to the protein accession?", false);
    setValidStrings_("decoy_string_position", ListUtils::create<String>("prefix,suffix"));
    registerFlag_("only_decoy", "Write only decoy proteins to the output database instead of a combined database.", false);

    registerStringOption_("type", "<choice>", "protein", "Type of sequence. RNA sequences may contain modification codes, which will be handled correctly if this is set to 'RNA'.", false);
    setValidStrings_("type", ListUtils::create<String>("protein,RNA"));

    registerStringOption_("method", "<choice>", "reverse", "Method by which decoy sequences are generated from target sequences. Note that all sequences are shuffled using the same random seed, ensuring that identical sequences produce the same shuffled decoy sequences. Shuffled sequences that produce highly similar output sequences are shuffled again (see shuffle_sequence_identity_threshold).", false);
    setValidStrings_("method", ListUtils::create<String>("reverse,shuffle"));
    registerIntOption_("shuffle_max_attempts", "<int>", 30, "shuffle: maximum attempts to lower the amino acid sequence identity between target and decoy for the shuffle algorithm", false, true);
    registerDoubleOption_("shuffle_sequence_identity_threshold", "<double>", 0.5, "shuffle: target-decoy amino acid sequence identity threshold for the shuffle algorithm. If the sequence identity is above this threshold, shuffling is repeated. In case of repeated failure, individual amino acids are 'mutated' to produce a different amino acid sequence.", false, true);

    registerStringOption_("seed", "<int>", '1', "Random number seed (use 'time' for system time)", false, true);

    StringList all_enzymes;
    ProteaseDB::getInstance()->getAllNames(all_enzymes);
    registerStringOption_("enzyme", "<enzyme>", "Trypsin", "Enzyme used for the digestion of the sample. Only applicable if parameter 'type' is 'protein'.",false);
    setValidStrings_("enzyme", all_enzymes);

    registerSubsection_("Decoy", "Decoy parameters section");

    // New options for neighbor peptide search
    registerTOPPSubsection_("Neighbor_Search", "Parameters for neighbor peptide search");
    registerInputFile_("Neighbor_Search:neighbor_in", "<file>","", "neighbor peptides are searched for in the Fasta file entered for candiate ", false);
    setValidFormats_("Neighbor_Search:neighbor_in", {"fasta"});
    registerOutputFile_("Neighbor_Search:out_neighbor", "<file>", "", "Output FASTA file where the neighbor peptide will be written to.",false);
    registerIntOption_("Neighbor_Search:missed_cleavages", "<int>", 0,"Number of missed cleavages",false);
    registerDoubleOption_("Neighbor_Search:mz_bin_size", "<num>", 0.05,"Sets the mz_bin_size of the neighbor peptide search.(the original study suggests high (0.05 Da) and low (1.0005079 Da) mz_bin_size)", false);
    registerDoubleOption_("Neighbor_Search:mass_tolerance", "<double>", 0.00001, "Tolerance of the mass of the neighbor peptide m1-m2 > tm", false, true);
    registerIntOption_("Neighbor_Search:min_peptide_length", "<int>", 5, "the minimum neighbor peptide length ", false);
    //setValidStrings_("Neighbor_Search:fragment_unit_ppm", {"true", "false"});
    //registerStringOption_("Neighbor_Search:fragment_unit_ppm", "<choice>", "true","calculates with dalton or ppm", false, true);
    registerDoubleOption_("Neighbor_Search:min_shared_ion_fraction", "<double>", 0.25, "Tolerance of the proportion of b and y ions shared by the neighbor peptide 2*B12/B1+B2 > ti", false, true);
  }


  Param getSubsectionDefaults_(const String& /* name */) const override
  {
    Param p = MRMDecoy().getDefaults();
    // change the default to also work with other proteases
    p.setValue("non_shuffle_pattern", "", "Residues to not shuffle (keep at a constant position when shuffling). Separate by comma, e.g. use 'K,P,R' here.");
    return p;
  }

  String getIdentifier_(const String& identifier, const String& decoy_string, const bool as_prefix)
  {
    if (as_prefix) return decoy_string + identifier;
    else return identifier + decoy_string;
  }
  

  ExitCodes main_(int, const char**) override
  {
    //-------------------------------------------------------------
    // parsing parameters
    //-------------------------------------------------------------
    enum SeqType {protein, RNA};
    StringList in = getStringList_("in");
    String out = getStringOption_("out");
    bool append = !getFlag_("only_decoy");
    bool shuffle = (getStringOption_("method") == "shuffle");
    String decoy_string = getStringOption_("decoy_string");
    bool decoy_string_position_prefix =
      (getStringOption_("decoy_string_position") == "prefix");
    SeqType input_type = SeqType::protein; //default to protein
    if (getStringOption_("type") == "RNA")
    {
      input_type = SeqType::RNA;
    }

    Param decoy_param = getParam_().copy("Decoy:", true);
    bool keepN = decoy_param.getValue("keepPeptideNTerm").toBool();
    bool keepC = decoy_param.getValue("keepPeptideCTerm").toBool();

    String keep_const_pattern = decoy_param.getValue("non_shuffle_pattern").toString();
    Int max_attempts = getIntOption_("shuffle_max_attempts");
    double identity_threshold = getDoubleOption_("shuffle_sequence_identity_threshold");

    // Set the seed for shuffling always to the same number, this
    // ensures that identical peptides get shuffled the same way
    // every time (without keeping track of them explicitly). This
    // will ensure that the total number of unique tryptic peptides
    // is identical in both databases.
    int seed;
    String seed_option(getStringOption_("seed"));
    if (seed_option == "time") seed = time(nullptr);
    else seed = seed_option.toInt();

    //-------------------------------------------------------------
    // reading input
    //-------------------------------------------------------------

    if (in.size() == 1)
    {
      OPENMS_LOG_WARN << "Warning: Only one FASTA input file was provided, which might not contain contaminants. "
               << "You probably want to have them! Just add the contaminant file to the input file list 'in'." << endl;
    }

    set<String> identifiers; // spot duplicate identifiers  // std::unordered_set<string> has slightly more RAM, but slightly less CPU

    FASTAFile f;
    f.writeStart(out);
    FASTAFile::FASTAEntry entry;

    // Create a ProteaseDigestion object for peptide digestion
    ProteaseDigestion digestion;
    String enzyme = getStringOption_("enzyme").trim();
    if ((input_type == SeqType::protein) && ! enzyme.empty()) { digestion.setEnzyme(enzyme); }
    
    
 // create neighbor peptides for the relevant peptides given in the FASTA from '-in'
    String neighbor_file = getStringOption_("Neighbor_Search:neighbor_in");
 // Check if the neighbor file option is provided
    if (! neighbor_file.empty())
    {
      if (input_type != SeqType::protein)
      {
        OPENMS_LOG_ERROR << "Parameter settings are invalid. When requesting neighbor peptides, the input type must be 'protein', not 'RNA'.\n";
        return INCOMPATIBLE_INPUT_DATA;
      }
      // Create a ProteaseDigestion object for neighbor peptide digestion
      ProteaseDigestion digestion_neighbor;
      String enzyme = getStringOption_("enzyme").trim();
      if ((input_type == SeqType::protein) && ! enzyme.empty()) { digestion_neighbor.setEnzyme(enzyme); }
      //-------------------------------------------------------------
      // parsing neighbor parameters
      //-------------------------------------------------------------
      String out_neighbor = getStringOption_("Neighbor_Search:out_neighbor");
      FASTAFile neig;
      neig.writeStart(out_neighbor);

      double mz_bin_size = getDoubleOption_("Neighbor_Search:mz_bin_size");
      double min_shared_ion_fraction = getDoubleOption_("Neighbor_Search:min_shared_ion_fraction");
      double mass_tolerance = getDoubleOption_("Neighbor_Search:mass_tolerance");
      int missed_cleavages = getIntOption_("Neighbor_Search:missed_cleavages");
      int min_peptide_length = getIntOption_("Neighbor_Search:min_peptide_length");
      digestion_neighbor.setMissedCleavages(missed_cleavages);
 // Vector to hold all digested peptides from the input files
      vector<AASequence> all_peptides;

      for (Size i = 0; i < in.size(); ++i)
      {
        //FASTAContainer<TFI_File> in_entries {in[i]};
        neig.readStart(in[i]);
        while (neig.readNext(entry))
        {
          // Vector to hold the peptides generated from digestion
          vector<AASequence> peptides;
          // Digest the current entry sequence into peptides
          digestion_neighbor.digest(AASequence::fromString(entry.sequence), peptides);
          // Add the generated peptides to the all_peptides vector
          all_peptides.insert(all_peptides.end(), peptides.begin(), peptides.end());
        }
      }
 // Load the neighbor peptides candidates from the neighbor file
      FASTAFile neighbor_fasta;
      vector<FASTAFile::FASTAEntry> neighbor_entries;
      neighbor_fasta.load(neighbor_file, neighbor_entries);
      vector<AASequence> digested_candidate_peptides;
      vector<AASequence> temp_peptides;
      for (auto& entry : neighbor_entries)
      {
        digestion_neighbor.digest(AASequence::fromString(entry.sequence), temp_peptides);  
        digested_candidate_peptides.insert(digested_candidate_peptides.end(), temp_peptides.begin(), temp_peptides.end());
      }

      // Remove peptides shorter than min_peptide_length amino acids
      digested_candidate_peptides.erase(remove_if(digested_candidate_peptides.begin(), digested_candidate_peptides.end(),
                                                         [min_peptide_length](const AASequence& peptide) { return peptide.size() < min_peptide_length; })
                                                        ,digested_candidate_peptides.end());

     // Creates a map of masses to positions from a vector
      map<double, vector<int>> mass_position_map = NeighborSeq::createMassPositionMap(digested_candidate_peptides);

      for (auto i = all_peptides.begin(); i != all_peptides.end(); ++i)
      {
        if (i->toString().find('X') == String::npos) 
        {
          // Finde position of neighbor peptides candidates
          vector<int> candiadate_position = NeighborSeq::findCandidatePositions(mass_position_map, i->getMonoWeight(), mass_tolerance);

          // Find neighbor peptides for the current peptide
          vector<int> result
            = NeighborSeq::findNeighborPeptides(*i, digested_candidate_peptides, candiadate_position, min_shared_ion_fraction, mz_bin_size);
          for (size_t j = 0; j < result.size(); ++j)
          {
            // Get the corresponding neighbor entry
            const auto& neighbor_entry = neighbor_entries[result[j]];
            // Create a new FASTAEntry with the neighbor peptide sequence
            FASTAFile::FASTAEntry new_entry(entry.identifier, entry.description, digested_candidate_peptides[result[j]].toString());
            // Write the neighbor peptide to the output file
            neig.writeNext(new_entry);
          } 
        }   
      }
      in.push_back(out_neighbor);
    }
    
      // Configure Enzymatic digestion
      // TODO: allow user-specified regex
      // check if decoy_string is common decoy string (e.g. decoy, rev, ...)
      String decoy_string_lower = decoy_string;
      decoy_string_lower.toLower();
      bool is_common = false;
      for (const auto& a : DecoyHelper::affixes)
      {
        if ((decoy_string_lower.hasPrefix(a) && decoy_string_position_prefix) || (decoy_string_lower.hasSuffix(a) && ! decoy_string_position_prefix))
        {
          is_common = true;
        }
      }
      // terminate, if decoy_string is not one of the allowed decoy strings (exit code 11)
      if (! is_common)
      {
        if (getFlag_("force"))
        {
          OPENMS_LOG_WARN << "Force Flag is enabled, decoys with custom decoy string (not in DecoyHelper::affixes) will not be detected.\n";
        }
        else
        {
          OPENMS_LOG_FATAL_ERROR << "Given decoy string is not allowed. Please use one of the strings in DecoyHelper::affixes as either prefix or "
                                    "suffix (case insensitive): \n";
          return INCOMPATIBLE_INPUT_DATA;
        }
      }
      MRMDecoy m;
      m.setParameters(decoy_param);

      Math::RandomShuffler shuffler(seed);
      for (Size i = 0; i < in.size(); ++i)
      {
        // check input files for decoys
        FASTAContainer<TFI_File> in_entries {in[i]};
        auto r = DecoyHelper::countDecoys(in_entries);
        // if decoys found, throw exception
        if (static_cast<double>(r.all_prefix_occur + r.all_suffix_occur) >= 0.4 * static_cast<double>(r.all_proteins_count))
        {
          // if decoys found, program terminates with exit code 11
          OPENMS_LOG_FATAL_ERROR << "Invalid input in " + in[i] + ": Input file already contains decoys." << '\n';
          return INCOMPATIBLE_INPUT_DATA;
        }

        f.readStart(in[i]);
        //-------------------------------------------------------------
        // calculations
        //-------------------------------------------------------------
        while (f.readNext(entry))
        {
          if (identifiers.find(entry.identifier) != identifiers.end())
          {
            OPENMS_LOG_WARN << "DecoyDatabase: Warning, identifier '" << entry.identifier << "' occurs more than once!" << endl;
          }
          identifiers.insert(entry.identifier);

          if (append) { f.writeNext(entry); }

          // identifier
          entry.identifier = getIdentifier_(entry.identifier, decoy_string, decoy_string_position_prefix);

          // sequence
          if (input_type == SeqType::RNA)
          {
            string quick_seq = entry.sequence;
            bool five_p = (entry.sequence.front() == 'p');
            bool three_p = (entry.sequence.back() == 'p');
            if (five_p) // we don't want to reverse terminal phosphates
            {
              quick_seq.erase(0, 1);
            }
            if (three_p) { quick_seq.pop_back(); }

            vector<String> tokenized;
            std::smatch m;
            std::string pattern = R"([^\[]|(\[[^\[\]]*\]))";
            std::regex re(pattern);

            while (std::regex_search(quick_seq, m, re))
            {
              tokenized.emplace_back(m.str(0));
              quick_seq = m.suffix();
            }

            if (shuffle) { shuffler.portable_random_shuffle(tokenized.begin(), tokenized.end()); }
            else // reverse
            {
              reverse(tokenized.begin(), tokenized.end()); // reverse the tokens
            }
            if (five_p) // add back 5'
            {
              tokenized.insert(tokenized.begin(), String("p"));
            }
            if (three_p) // add back 3'
            {
              tokenized.emplace_back("p");
            }
            entry.sequence = ListUtils::concatenate(tokenized, "");
          }
          else // protein input
          {
            // if (terminal_aminos != "none")
            if (enzyme != "no cleavage" && (keepN || keepC))
            {
              std::vector<AASequence> peptides;
              digestion.digest(AASequence::fromString(entry.sequence), peptides);
              String new_sequence = "";
              for (auto const& peptide : peptides)
              {
                // TODO why are the functions from TargetedExperiment and MRMDecoy not anywhere more general?
                //  No soul would look there.
                if (shuffle)
                {
                  OpenMS::TargetedExperiment::Peptide p;
                  p.sequence = peptide.toString();
                  OpenMS::TargetedExperiment::Peptide decoy_p = m.shufflePeptide(p, identity_threshold, seed, max_attempts);
                  new_sequence += decoy_p.sequence;
                }
                else
                {
                  OpenMS::TargetedExperiment::Peptide p;
                  p.sequence = peptide.toString();
                  OpenMS::TargetedExperiment::Peptide decoy_p = MRMDecoy::reversePeptide(p, keepN, keepC, keep_const_pattern);
                  new_sequence += decoy_p.sequence;
                }
              }
              entry.sequence = new_sequence;
            }
            else
            {
              // sequence
              if (shuffle)
              {
                shuffler.seed(seed); // identical proteins are shuffled the same way -> re-seed
                shuffler.portable_random_shuffle(entry.sequence.begin(), entry.sequence.end());
              }
              else // reverse
              {
                entry.sequence.reverse();
              }
            }
          }

          //-------------------------------------------------------------
          // writing output
          //-------------------------------------------------------------
          f.writeNext(entry);
        } // next protein
      }   // input files
    
    return EXECUTION_OK;
  }
};


int main(int argc, const char** argv)
{
  TOPPDecoyDatabase tool;
  return tool.main(argc, argv);
}

/// @endcond
