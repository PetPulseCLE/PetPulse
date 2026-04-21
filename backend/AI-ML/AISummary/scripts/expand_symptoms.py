"""One-shot script: append Phase 3 Heart/Respiratory entries to symptoms.json."""
import json
from pathlib import Path

DATASET = Path(__file__).resolve().parent.parent / "symptoms.json"

HEART_CLINICAL = [
    "Auscult for left apical systolic murmur consistent with degenerative mitral valve disease in small-breed dogs.",
    "Measure vertebral heart score (VHS) on right lateral thoracic radiograph; values greater than 10.5 suggest cardiomegaly.",
    "Obtain echocardiogram to assess left atrial to aortic ratio (LA:Ao); greater than 1.6 indicates significant left atrial enlargement.",
    "Measure NT-proBNP to differentiate cardiac from respiratory causes of dyspnea in cats and dogs.",
    "Evaluate for a gallop rhythm (S3); its presence is almost always pathologic in dogs and cats.",
    "Pulse deficits noted on simultaneous femoral pulse and apical auscultation suggest underlying arrhythmia.",
    "Initiate pimobendan 0.25 mg/kg PO q12h for ACVIM Stage B2 or Stage C myxomatous mitral valve disease.",
    "Add furosemide 1-2 mg/kg PO q12h when thoracic radiographs document pulmonary edema.",
    "Counsel owner to track resting respiratory rate at home; sustained values above 30 bpm warrant reassessment.",
    "Rule out hypertrophic cardiomyopathy in cats presenting with unexplained tachypnea, gallop, or murmur.",
    "Obtain ECG to characterize sustained tachycardia or document ventricular premature complexes.",
    "Screen taurine levels in breeds predisposed to dilated cardiomyopathy (Cocker Spaniel, Golden Retriever).",
    "Transition to a moderately sodium-restricted diet once congestive heart failure is confirmed.",
    "Recheck renal values and electrolytes 5-7 days after initiating or escalating loop diuretic therapy.",
    "Assess for ascites via ballottement when right-sided congestive heart failure is suspected.",
    "Evaluate for concurrent systemic hypertension; target systolic pressure below 160 mmHg in feline cardiomyopathy.",
    "Consider spironolactone as part of quadruple therapy in advanced congestive heart failure.",
    "Perform therapeutic abdominocentesis when ascites compromises respiratory mechanics.",
    "Recommend MYBPC3 genetic screening for HCM in Maine Coons and Ragdolls.",
    "Schedule annual cardiac auscultation and screening echocardiogram for predisposed breeds from age 5 onward.",
]

HEART_OWNER = [
    "My dog has been coughing, especially at night and when he gets excited.",
    "My cat collapsed in the hallway this morning and was slow to recover.",
    "He gets tired after walking half a block and his tongue turns a bluish color.",
    "I can feel my dog's heart beating irregularly when he lies on my lap.",
    "Her belly looks bloated even though she has been losing weight.",
    "He wakes up panting in the middle of the night even when the room is cool.",
    "My cat has been hiding more and sometimes breathes with her mouth open.",
    "Her gums look pale when she first lifts her head in the morning.",
    "He fainted briefly when the doorbell rang yesterday.",
    "My dog refuses to climb the stairs and stops partway up now.",
    "My dog's back legs suddenly went limp and his paws felt cold to the touch.",
    "She seems to breathe harder than usual when sleeping on her side.",
    "He used to love fetch but now he lies down after just one throw.",
    "My cat has lost weight even though she is eating the same amount.",
    "There is a strange vibration I can feel under his ribcage when I pet him.",
]

RESPIRATORY_CLINICAL = [
    "Obtain three-view thoracic radiographs to characterize lung pattern as alveolar, interstitial, bronchial, or mixed.",
    "Collect transtracheal or bronchoalveolar lavage for cytology and culture in chronic lower airway disease.",
    "Evaluate arytenoid motion under light anesthesia to confirm or rule out laryngeal paralysis.",
    "Differentiate stertor (nasopharyngeal) from stridor (laryngeal) to localize upper airway obstruction.",
    "SpO2 below 94% on room air indicates clinically significant hypoxemia; begin supplemental oxygen.",
    "In brachycephalic breeds, assess for stenotic nares, elongated soft palate, and everted laryngeal saccules.",
    "Trial doxycycline 5 mg/kg PO q12h when Mycoplasma or Bordetella lower airway infection is suspected.",
    "Manage feline asthma with inhaled fluticasone via spacer and a short tapering course of oral prednisolone.",
    "Screen any coughing dog in endemic regions with heartworm antigen and microfilaria testing.",
    "Perform thoracocentesis when pleural effusion produces a restrictive respiratory pattern.",
    "Measure triglycerides and obtain cytology on pleural fluid to differentiate chylothorax from other effusions.",
    "Prominent expiratory effort with abdominal push is a hallmark of lower airway (bronchial) disease.",
    "Open-mouth breathing in a cat is a medical emergency until proven otherwise.",
    "Consider CT imaging for suspected nasal neoplasia, fungal rhinitis, or foreign body.",
    "Document a resting respiratory rate baseline; sustained values above 40 bpm warrant further diagnostics.",
    "Arterial blood gas analysis with A-a gradient calculation clarifies severe or refractory dyspnea.",
    "Pulse oximetry trending is useful but less reliable in pigmented mucosa or poorly perfused patients.",
    "Consider bronchoscopy in dogs with chronic cough unresponsive to empirical therapy.",
    "Avoid oversedation in airway emergencies; butorphanol 0.2 mg/kg IV is generally safer than full mu-agonists.",
    "In cats with bronchial disease, rule out heartworm-associated respiratory disease (HARD).",
]

RESPIRATORY_OWNER = [
    "My dog has been coughing up white foam after drinking water.",
    "She makes a honking noise when she gets excited or pulls on the leash.",
    "My cat crouches low and her sides move in and out hard when she breathes.",
    "He snores so loudly at night the whole family can hear him.",
    "My pug turns blue around the lips after short walks in warm weather.",
    "There is a clear discharge coming from both of his nostrils constantly.",
    "She sneezes in fits of five or six in a row, multiple times a day.",
    "My cat wheezes when she runs and then has to stop and catch her breath.",
    "He has been breathing through his mouth even when he is just resting.",
    "My dog gags and retches but nothing comes up.",
    "My French bulldog collapsed after playing in the yard on a hot day.",
    "She has been congested and eating less for about three days now.",
    "His breathing sounds raspy and you can hear it from across the room.",
    "My cat had a nosebleed this morning and seemed very quiet afterwards.",
    "He coughs so hard he almost falls over and then he is exhausted afterwards.",
]


def main() -> None:
    with DATASET.open() as f:
        data = json.load(f)

    new_entries: list[dict] = []
    for text in HEART_CLINICAL:
        new_entries.append(
            {"text": text, "condition": "Heart Issues", "record_type": "Clinical Notes"}
        )
    for text in HEART_OWNER:
        new_entries.append(
            {"text": text, "condition": "Heart Issues", "record_type": "Owner Observation"}
        )
    for text in RESPIRATORY_CLINICAL:
        new_entries.append(
            {"text": text, "condition": "Respiratory Issues", "record_type": "Clinical Notes"}
        )
    for text in RESPIRATORY_OWNER:
        new_entries.append(
            {"text": text, "condition": "Respiratory Issues", "record_type": "Owner Observation"}
        )

    existing_texts = {d.get("text") for d in data}
    added = 0
    for entry in new_entries:
        if entry["text"] in existing_texts:
            continue
        data.append(entry)
        existing_texts.add(entry["text"])
        added += 1

    with DATASET.open("w") as f:
        json.dump(data, f, indent=4)

    print(f"Appended {added} entries. Total now: {len(data)}")


if __name__ == "__main__":
    main()
